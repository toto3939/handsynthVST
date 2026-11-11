# main.py

from hand_tracker import HandTracker
from gesture_features import compute_hand_openness, compute_thumb_value
from smoothing import SignalSmoother
from osc_sender import OscSender
from config import OSC_IP, OSC_PORT

def main():
    # Gesture tracking
    tracker = HandTracker()

    # Separate smoothing filters for each hand
    smooth_hand_left = SignalSmoother(alpha=0.7)
    smooth_hand_right = SignalSmoother(alpha=0.7)

    smooth_thumb_left = SignalSmoother(alpha=0.6)
    smooth_thumb_right = SignalSmoother(alpha=0.6)


    # OSC
    osc = OscSender(ip=OSC_IP, port=OSC_PORT, debug=False) # debug=True to print OSC messages values in console

    while True:
        # Just one reading for each frame
        frame, hands = tracker.get_frame_and_hands()

        if frame is not None:
            # Hand gesture processing
            for hand in hands:
                label = hand["label"]          # "Left" or "Right"
                landmarks = hand["landmarks"]

                openness = compute_hand_openness(landmarks)

                if label == "Left":    
                    value = smooth_hand_left.process(openness)
                    osc.send("/hand/left/open", value)
                    thumb_raw = compute_thumb_value(landmarks)
                    thumb = smooth_thumb_left.process(thumb_raw)
                    print(f"MANO: {label} | OPEN: {value:.3f} | THUMB: {thumb:.3f}")
                    osc.send("/hand/left/thumb", thumb)

                elif label == "Right":
                    value = smooth_hand_right.process(openness)
                    osc.send("/hand/right/open", value)
                    thumb_raw = compute_thumb_value(landmarks)
                    thumb = smooth_thumb_right.process(thumb_raw)
                    print(f"MANO: {label} | OPEN: {value:.3f} | THUMB: {thumb:.3f}")
                    osc.send("/hand/right/thumb", thumb)
            tracker.show(frame)

        if tracker.should_quit():
            break

    tracker.release()


if __name__ == "__main__":
    main()
