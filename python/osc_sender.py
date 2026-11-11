# osc_sender.py

from pythonosc.udp_client import SimpleUDPClient

class OscSender:
    def __init__(self, ip="127.0.0.1", port=9000, debug=False, debug_filter=None):
        """
        ip: OSC destination (localhost for tests)
        port: UDP port
        debug: if True prints the sent messages
        """
        self.client = SimpleUDPClient(ip, port)
        self.debug = debug
        self.debug_filter = debug_filter

        
    def _should_debug(self, address):
        if not self.debug:
                return False
        if self.debug_filter is None:
                return True
        return any(f in address for f in self.debug_filter)

    def _debug_print(self, address, value):
        if self._should_debug(address):
            if isinstance(value, (float, int)):
                print(f"[OSC] {address} -> {value:.3f}")
            else:
                print(f"[OSC] {address} -> {value}")


    def send(self, address, value):
        """
        Sends a single float/int value
        """
        try:
            self.client.send_message(address, value)
            self._debug_print(address, value)
        except Exception as e:
            print(f"[OSC ERROR] {e}")

    def send_bundle(self, address, values):
        """
        Sends multiple values under the same address
        (useful if in the future you want to send vectors)
        """
        try:
            self.client.send_message(address, values)
            self._debug_print(address, values)
        except Exception as e:
            print(f"[OSC ERROR] {e}")