"""Portable Windows launcher for the shared Stillink services."""
import ipaddress
import json
import logging
import queue
import socket
import sys
import tempfile
import threading
from pathlib import Path

import album
import app
import weather

PORT = 8787


class WindowsHandler(app.Handler):
    def log_message(self, format, *args):
        logging.info("%s %s", self.address_string(), format % args)


def service_addresses(port=PORT):
    addresses = set()
    for entry in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
        ip = ipaddress.ip_address(entry[4][0])
        if not ip.is_loopback and not ip.is_link_local and not ip.is_unspecified:
            addresses.add(str(ip))
    return [f"http://{ip}:{port}" for ip in sorted(addresses)]


class Service:
    def __init__(self, directory, host="0.0.0.0", port=PORT, music_port=8790):
        app.ALBUM = album.Store(Path(directory) / "data")
        app.MUSIC.font_path = weather.find_font()
        self.http = app.ThreadingHTTPServer((host, port), WindowsHandler)
        self.http.daemon_threads = True
        try:
            self.music = app.MUSIC.start(host=host, port=music_port)
        except Exception:
            self.http.server_close()
            raise
        self.thread = threading.Thread(target=self.http.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.http.shutdown()
        self.http.server_close()
        self.thread.join(timeout=3)
        app.MUSIC.stop()


def packaged_check(output):
    """Exercise the bundled server and fonts without opening a window."""
    from urllib.request import ProxyHandler, build_opener
    from PIL import ImageFont
    import tkinter as tk
    import pystray
    root = tk.Tk()
    root.withdraw()
    root.destroy()
    assert pystray.Icon
    with tempfile.TemporaryDirectory() as directory:
        service = Service(directory, host="127.0.0.1", port=0, music_port=0)
        try:
            base = f"http://127.0.0.1:{service.http.server_port}"
            opener = build_opener(ProxyHandler({}))
            with opener.open(base + "/health", timeout=5) as response:
                health = json.load(response)
            assert health["status"] == "ok" and health["services"]["album"]["available"]
            assert health["services"]["music"]["receiver_ready"]
            ImageFont.truetype(weather.find_font(), 20).getmask("天气相簿")
            with opener.open(base + "/v1/album/photos", timeout=5) as response:
                assert json.load(response) == {"photos": []}
            Path(output).write_text(json.dumps({"status": "ok"}), encoding="utf-8")
        finally:
            service.close()


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--packaged-check":
        packaged_check(sys.argv[2])
        return
    import tkinter as tk
    from tkinter import ttk, messagebox
    import webbrowser
    import pystray
    from PIL import Image, ImageDraw

    directory = Path(sys.executable).parent if getattr(sys, "frozen", False) else Path(__file__).parent
    root = tk.Tk()
    root.withdraw()
    root.title("Stillink 额外服务")
    root.geometry("480x300")
    root.minsize(460, 300)
    try:
        logging.basicConfig(filename=directory / "services.log", level=logging.INFO,
                            format="%(asctime)s %(levelname)s %(message)s")
        service = Service(directory)
    except Exception as exc:
        logging.exception("Service startup failed")
        messagebox.showerror("Stillink 服务启动失败", f"{exc}\n\n请检查是否已运行服务、8787 / 8790 端口是否被占用，以及解压目录是否可写。", parent=root)
        root.destroy()
        return

    events = queue.Queue()
    box = ttk.Frame(root, padding=22)
    box.pack(fill="both", expand=True)
    ttk.Label(box, text="服务已启动", font=("Microsoft YaHei UI", 16, "bold")).pack(anchor="w")
    ttk.Label(box, text="将下面地址填入设备设置中的「额外服务支持」。").pack(anchor="w", pady=(10, 8))
    address = tk.StringVar()
    selector = ttk.Combobox(box, textvariable=address, state="readonly", font=("Segoe UI", 12))
    selector.pack(fill="x")
    status = tk.StringVar()

    def refresh():
        try:
            addresses = service_addresses()
        except OSError:
            addresses = []
        selector["values"] = addresses
        address.set(addresses[0] if addresses else f"http://127.0.0.1:{PORT}")
        status.set("请选择与 ESP 同一局域网的地址。" if addresses else "未检测到局域网地址，请连接 Wi-Fi 或网线后刷新。")

    def copy():
        root.clipboard_clear()
        root.clipboard_append(address.get())
        status.set("地址已复制。")

    buttons = ttk.Frame(box)
    buttons.pack(fill="x", pady=12)
    ttk.Button(buttons, text="复制地址", command=copy).pack(side="left")
    ttk.Button(buttons, text="检查服务", command=lambda: webbrowser.open(address.get() + "/health")).pack(side="left", padx=8)
    ttk.Button(buttons, text="刷新地址", command=refresh).pack(side="left")
    ttk.Label(box, textvariable=status, wraplength=430).pack(anchor="w")
    ttk.Label(box, text="关闭窗口后继续在托盘运行，右键托盘图标可退出。\n首次运行请允许专用网络访问；相簿保存在 data 文件夹。", wraplength=430).pack(anchor="w", pady=(12, 0))

    image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((2, 2, 62, 62), radius=14, fill="#3f7d6b")
    draw.rounded_rectangle((15, 12, 49, 52), radius=4, outline="white", width=4)
    draw.line((22, 25, 42, 25), fill="white", width=3)
    draw.line((22, 36, 42, 36), fill="white", width=3)
    icon = pystray.Icon("StillinkServices", image, "Stillink 额外服务 · 运行中", pystray.Menu(
        pystray.MenuItem("显示服务地址", lambda *_: events.put("show"), default=True),
        pystray.MenuItem("退出", lambda *_: events.put("quit"))))
    tray_thread = threading.Thread(target=icon.run, daemon=True)
    tray_thread.start()

    def poll():
        while not events.empty():
            action = events.get_nowait()
            if action == "quit":
                root.quit()
                return
            refresh()
            root.deiconify()
            root.lift()
        root.after(200, poll)

    root.protocol("WM_DELETE_WINDOW", root.withdraw)
    refresh()
    root.deiconify()
    root.after(200, poll)
    try:
        root.mainloop()
    finally:
        icon.stop()
        service.close()
        root.destroy()


if __name__ == "__main__":
    main()
