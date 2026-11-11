# hand_tracker.py

import time
import cv2
import tkinter as tk
import mediapipe as mp
from config import CAMERA_INDEX, MAX_HANDS, MIN_DET_CONF, MIN_TRACK_CONF


class HandTracker:
    def __init__(self):
        # Camera
        self.SHOW_CAMERA = True  # or False
        self.cap = cv2.VideoCapture(CAMERA_INDEX, cv2.CAP_DSHOW)
        if not self.cap.isOpened():
            raise RuntimeError(f"Impossibile aprire la webcam {CAMERA_INDEX}")

        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        self.cap.set(cv2.CAP_PROP_FPS, 30) 
        time.sleep(0.3)  # warm-up webcam

        self.window_name = "Hand Tracker"
        cv2.namedWindow(self.window_name, cv2.WINDOW_AUTOSIZE)
        self.window_positioned = False
        self.display_size = 540  # size of the longer side

        # MediaPipe
        self.mp_hands = mp.solutions.hands
        self.hands = self.mp_hands.Hands(
            max_num_hands=MAX_HANDS,
            model_complexity=0,
            min_detection_confidence=MIN_DET_CONF,
            min_tracking_confidence=MIN_TRACK_CONF
        )
        self.drawer = mp.solutions.drawing_utils
        self.label_streak = {
           "Left": 0,
           "Right": 0
       }

    
    def get_frame_and_hands(self):
     success, frame = self.cap.read()

     if not success or frame is None:
        return None, []

     frame = cv2.flip(frame, 1)

     frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
     result = self.hands.process(frame_rgb)

     hands_data = []

     # Raccogli le etichette rilevate nel frame corrente
     detected_labels = []

     if result.multi_handedness:
        detected_labels = [
            h.classification[0].label
            for h in result.multi_handedness
        ]

     # Conta i frame consecutivi per ogni etichetta
     for label in self.label_streak:
        if label in detected_labels:
            self.label_streak[label] += 1
        else:
            self.label_streak[label] = 0

     if result.multi_hand_landmarks and result.multi_handedness:

        for landmarks, handedness in zip(
            result.multi_hand_landmarks,
            result.multi_handedness
        ):
            label = handedness.classification[0].label

            # Ignora le etichette presenti da un solo frame
            if self.label_streak[label] < 2:
                continue

            # Disegna i landmark
            self.drawer.draw_landmarks(
                frame,
                landmarks,
                self.mp_hands.HAND_CONNECTIONS
            )

            hands_data.append({
                "label": label,
                "landmarks": landmarks.landmark
            })

     return frame, hands_data


    def position_window(self):
        root = tk.Tk()
        root.withdraw()

        screen_w = root.winfo_screenwidth()
        screen_h = root.winfo_screenheight()

        root.destroy()

        margin = 0
        x = max(0, screen_w - self.display_w- 10)
        y = 0

        cv2.moveWindow(self.window_name, x, y)

    def show(self, frame):
        if not self.SHOW_CAMERA or frame is None or frame.size == 0:
            return

        h, w, _ = frame.shape

        scale = self.display_size / max(w, h)

        self.display_w = int(w * scale)
        self.display_h = int(h * scale)

        display = cv2.resize(frame, (self.display_w, self.display_h))

        cv2.imshow(self.window_name, display)

        if not self.window_positioned:
            self.position_window()
            self.window_positioned = True

    def should_quit(self):
        return cv2.waitKey(1) & 0xFF == ord('q')

    def release(self):
        if self.cap.isOpened():
            self.cap.release()
        cv2.destroyAllWindows()