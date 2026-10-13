"""Read Windows GSMTC sessions and broadcast metadata and original artwork."""
import argparse
import asyncio
import json
import base64
import socket
from dataclasses import dataclass
import sys
import time

from protocol import packets
from runtime import BackgroundInstance, load_config, setup_background_log
from tray import MusicTray

@dataclass
class Track:
    title: str
    artist: str = ""
    album: str = ""
    player: str = ""
    cover: bytes = b""
    playing: bool = True
    number: int = 0

    @property
    def key(self):
        return self.player, self.title, self.artist, self.album, self.number


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


def announcement(track):
    if not track:
        return json.dumps(dict(playing=False,title="",artist="",album="",player="",cover="")).encode()
    return json.dumps(dict(playing=track.playing,title=track.title,artist=track.artist,album=track.album,player=track.player,
        cover=base64.b64encode(track.cover if len(track.cover)<=4*1024*1024 else b"").decode()),ensure_ascii=False).encode()


async def send(sock,target,body):
    # Repeat the burst once; the receiver merges reordered/duplicated chunks.
    chunks=packets(body)
    for repeat in range(2):
        for packet in chunks:
            sock.sendto(packet,target)
            await asyncio.sleep(0.001)


async def run(args):
    media=WindowsMedia();await media.start()
    if args.inspect:
        track=await media.read(args.player)
        print(json.dumps(None if not track else dict(title=track.title,artist=track.artist,album=track.album,
              player=track.player,playing=track.playing,cover_bytes=len(track.cover)),ensure_ascii=False))
        return
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET,socket.SO_BROADCAST,1)
    target=(args.target,args.port)
    last_key=None;cached=None;last_body=None;last_sent=float("-inf")
    print(f"Windows 音乐广播已启动：UDP {args.target}:{args.port}。Ctrl+C 退出。",flush=True)
    try:
        while not args.instance or not args.instance.stopping():
            try:
                track=await media.read(args.player,with_cover=False)
                if track:
                    if track.key!=last_key or not cached.cover:
                        cached=await media.read(args.player)
                        if not cached or cached.key!=track.key:
                            await asyncio.sleep(3);continue
                        last_key=track.key
                    cached.playing=track.playing
                else:
                    cached=None;last_key=None
                body=announcement(cached)
                if body!=last_body or time.monotonic()-last_sent>=10:
                    await send(sock,target,body)
                    last_body=body;last_sent=time.monotonic()
                    if cached:print(f"已广播：{cached.title} · {cached.artist}",flush=True)
                if args.once:return
            except Exception as exc:
                if args.once:raise
                print(f"广播失败：{exc}",flush=True)
            await asyncio.sleep(3)
    finally:sock.close()


def main():
    setup_background_log()
    parser=argparse.ArgumentParser(description="Windows 音乐信息和封面 UDP 广播")
    parser.add_argument("--inspect",action="store_true",help="只显示当前系统媒体信息，不发送广播")
    parser.add_argument("--once",action="store_true",help="只广播一次后退出")
    parser.add_argument("--stop",action="store_true",help="停止后台广播")
    args=parser.parse_args()
    instance=None
    tray=None
    try:
        if sys.platform != "win32": raise RuntimeError("音乐读取功能需要 Windows 10 / 11")
        if args.stop:
            instance=BackgroundInstance();instance.stop();return 0
        for key,value in load_config().items():setattr(args,key,value)
        args.instance=None
        if not args.inspect and not args.once:
            instance=BackgroundInstance()
            if not instance.acquire():return 0
            args.instance=instance
            tray=MusicTray(instance)
            tray.start()
        asyncio.run(run(args))
    except KeyboardInterrupt:pass
    except Exception as exc:print(f"错误：{exc}",file=sys.stderr);return 1
    finally:
        if tray:tray.close()
        if instance:instance.close()
    return 0


if __name__=="__main__":raise SystemExit(main())
