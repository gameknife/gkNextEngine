let interval: ReturnType<typeof setInterval> | undefined;
self.onmessage = ({ data }: MessageEvent<boolean>) => {
    clearInterval(interval);
    interval = data ? setInterval(() => self.postMessage("tick"), 500) : undefined;
};
