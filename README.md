# HandSynth

**HandSynth** is a work-in-progress gesture-controlled audio plugin that uses webcam hand tracking to control audio effects in real time.

Originally developed as a standalone application using **Python, MediaPipe, OSC and SuperCollider** for audio processing, the project is now being developed as a **JUCE-based VST plugin**.

### Features
- Real-time hand tracking with OpenCV and MediaPipe.
- OSC communication between Python and JUCE.
- Independent left- and right-hand gesture controls.
- Four audio parameters controlled by hand gestures:
  - Left hand: LPF Cutoff and Flanger.
  - Right hand: Chorus and Distortion.

### Technologies
- Python
- OpenCV & MediaPipe
- OSC (python-osc)
- C++ / JUCE
- SuperCollider (original standalone version)

### Status
<escape>🚧</escape> **Work in Progress** – The project is currently being developed and refined. Features and implementation may change.

---

*Turning hand gestures into sound.*
