# smoothing.py

class SignalSmoother:
    def __init__(self, alpha=0.3):
        self.alpha = alpha
        self.prev = None

    def process(self, value):
        if self.prev is None:
            self.prev = value
            return value

        output = self.alpha * value + (1 - self.alpha) * self.prev
        self.prev = output
        return output