"""Read Windows GSMTC sessions and upload only when the playing track changes."""
import argparse
import asyncio
import json
import sys
import time
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

from card import Track, pack_frame, render_card

OPENER = build_opener(ProxyHandler({}))


class WindowsMedia:
    async def start(self):
        if sys.platform != "win32": raise RuntimeError("音乐读取功能需要 Windows 10 / 11")
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionManager
        self.manager = await GlobalSystemMediaTransportControlsSessionManager.request_async()

    async def read(self, player_filter="", with_cover=True):
        from winrt.windows.media.control import GlobalSystemMediaTransportControlsSessionPlaybackStatus as Status
        sessions = list(self.manager.get_sessions())
        if player_filter:
            sessions = [s for s in sessions if player_filter.lower() in s.source_app_user_model_id.lower()]
        playing = [s for s in sessions if s.get_playback_info().playback_status == Status.PLAYING]
        current = self.manager.get_current_session()
        session = next((s for s in playing if current and s.source_app_user_model_id == current.source_app_user_model_id),
                       playing[0] if playing else sessions[0] if sessions else None)
        if not session: return None
        properties = await session.try_get_media_properties_async()
        if not properties or not properties.title: return None
        track = Track(properties.title, properties.artist, properties.album_title,
                      session.source_app_user_model_id, playing=session.get_playback_info().playback_status == Status.PLAYING,
                      number=properties.track_number)
        if with_cover and properties.thumbnail:
            from winrt.windows.storage.streams import DataReader
            try:
                stream = await properties.thumbnail.open_read_async()
                try:
                    reader = DataReader(stream.get_input_stream_at(0))
                    try:
                        count = int(stream.size)
                        if 0 < count <= 16 * 1024 * 1024:
                            loaded = await reader.load_async(count)
                            data = bytearray(loaded)
                            reader.read_bytes(data)
                            track.cover = bytes(data)
                    finally:
                        reader.close()
                finally:
                    stream.close()
            except OSError:
                # A player may advertise artwork before the stream is available.
                pass
        return track


class SyncState:
    """Coalesce rapid track changes; pause/resume does not refresh the screen."""
    def __init__(self, interval=60, settle=3):
        self.interval, self.settle = interval, settle
        self.candidate = None; self.since = 0
        self.sent = None; self.last_sent = float("-inf"); self.retry_at = 0

    def due(self, track, now):
        if not track or not track.playing:
            self.candidate = None
            return False
        if track.key != self.candidate:
            self.candidate, self.since = track.key, now
        return (track.key != self.sent and now - self.since >= self.settle and
                now - self.last_sent >= self.interval and now >= self.retry_at)

    def success(self, track, now):
        self.sent, self.last_sent, self.retry_at = track.key, now, 0

    def failed(self, now):
        self.retry_at = now + 30


def device_request(base, path, payload=None, kind="application/json"):
    request = Request(base + path, data=payload,
                      headers={"Content-Type": kind} if payload is not None else {})
    with OPENER.open(request, timeout=20) as response:
        return response.read()


def check_device(base, disable_weather):
    device_request(base, "/api/info")
    try:
        weather = json.loads(device_request(base, "/api/weather"))
    except HTTPError as exc:
        if exc.code == 404: return
        raise
    if weather.get("enabled"):
        if not disable_weather:
            raise RuntimeError("请先在设备网页关闭自动天气，或使用 --disable-weather，避免天气覆盖音乐卡片")
        device_request(base, "/api/weather", b'{"enabled":false}')
        print("已关闭设备自动天气；退出工具后可在网页重新开启。", flush=True)


def upload(base, track, font_path, preview):
    card = render_card(track, font_path)
    if preview: card.save(preview, "PNG")
    result = device_request(base, "/api/frame", pack_frame(card), "application/octet-stream")
    if result.strip() != b"OK": raise RuntimeError(f"设备未接受图片：{result.decode(errors='replace')}")


async def run(args):
    media = WindowsMedia(); await media.start()
    if args.inspect:
        track = await media.read(args.player)
        print(json.dumps(None if not track else dict(title=track.title, artist=track.artist, album=track.album,
              player=track.player, playing=track.playing, cover_bytes=len(track.cover)), ensure_ascii=False))
        return
    base = args.device.rstrip("/")
    if not base.startswith(("http://", "https://")): base = "http://" + base
    await asyncio.to_thread(check_device, base, args.disable_weather)
    state = SyncState(args.interval)
    print("音乐同步已启动，等待播放器。Ctrl+C 退出。", flush=True)
    while True:
        try:
            track = await media.read(args.player, with_cover=False)
            if args.once:
                if not track or not track.playing: raise RuntimeError("没有正在播放的音乐，请先开始播放")
            if args.once or state.due(track, time.monotonic()):
                full = await media.read(args.player)
                if full and full.playing and full.key == track.key:
                    await asyncio.to_thread(upload, base, full, args.font, args.preview)
                    state.success(full, time.monotonic())
                    print(f"图片已接收：{full.title} · {full.artist}（设备随后刷新）", flush=True)
                    if args.once: return
        except Exception as exc:
            if args.once: raise
            state.failed(time.monotonic())
            print(f"同步失败，30 秒后重试：{exc}", flush=True)
            await asyncio.sleep(30)
        await asyncio.sleep(3)


def main():
    parser = argparse.ArgumentParser(description="Windows 音乐信息 → Stillink 专辑卡片")
    parser.add_argument("--device", help="设备 IP 或 HTTP 地址")
    parser.add_argument("--player", default="", help="播放器 ID 包含的文字；默认选当前播放会话")
    parser.add_argument("--interval", type=int, default=60, help="最小刷新间隔秒数，至少 30，默认 60")
    parser.add_argument("--disable-weather", action="store_true", help="关闭设备自动天气后同步音乐")
    parser.add_argument("--inspect", action="store_true", help="只显示当前系统媒体信息，不操作设备")
    parser.add_argument("--once", action="store_true", help="上传当前歌曲后退出")
    parser.add_argument("--preview", type=Path, help="保存最新卡片的 PNG 预览")
    parser.add_argument("--font", default="", help="中文字体路径，默认使用微软雅黑")
    args = parser.parse_args()
    if not args.inspect and not args.device: parser.error("请提供 --device，或使用 --inspect")
    if args.interval < 30: parser.error("--interval 至少为 30 秒")
    try: asyncio.run(run(args))
    except KeyboardInterrupt: pass
    except Exception as exc:
        print(f"错误：{exc}", file=sys.stderr); return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
