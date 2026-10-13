"""Windows notification-area icon for the music broadcaster."""
import threading


class MusicTray:
    def __init__(self, instance):
        from PIL import Image, ImageDraw
        import pystray

        image = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rounded_rectangle((2, 2, 62, 62), radius=14, fill="#c7352b")
        draw.line((29, 45, 29, 19, 48, 15, 48, 41), fill="white", width=5)
        draw.ellipse((16, 39, 31, 51), fill="white")
        draw.ellipse((35, 35, 50, 47), fill="white")
        self.instance = instance
        self.icon = pystray.Icon("StillinkMusic", image, "Stillink 音乐广播",
                                 pystray.Menu(pystray.MenuItem("退出", self.exit)))
        self.thread = threading.Thread(target=self.icon.run, name="music-tray", daemon=True)

    def start(self):
        self.thread.start()

    def exit(self, icon, item):
        self.instance.stop()
        icon.stop()

    def close(self):
        self.icon.stop()
        self.thread.join(timeout=3)
