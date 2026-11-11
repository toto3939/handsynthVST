# gesture_features.py

FINGERTIPS = [8, 12, 16, 20]

THUMB_TIP = 4
THUMB_IP = 3
INDEX_MCP = 5

PALM_POINTS = [0, 5, 9, 13, 17]

FINGERTIP_WEIGHTS = {
    8:  0.35,  # index
    12: 0.35,  # middle
    16: 0.15,  # ring
    20: 0.15   # pinky
}

MIN_OPENNESS = 0.035
MAX_OPENNESS = 0.26

MIN_THUMB_DIST = 0.2
MAX_THUMB_DIST = 0.40

def normalize(value, min_val, max_val):
    if max_val == min_val:
            return 0.0
    value_clamped = max(min_val, min(value, max_val))
    return (value_clamped - min_val) / (max_val - min_val)

def compute_hand_openness(landmarks):
    # Computing the coordinates of a virtual "palm center" as the mean of key palm landmarks
    palm_x = sum(landmarks[i].x for i in PALM_POINTS) / len(PALM_POINTS) # mean x coordinate of a virtual palm center
    palm_y = sum(landmarks[i].y for i in PALM_POINTS) / len(PALM_POINTS) # mean y coordinate of a virtual palm center

    # Accumulators for weighted fingertip distances and total weight
    weighted_sum = 0.0
    weight_total = 0.0

    # Compute weighted fingertip-to-palm distances (index & middle prioritized)
    for idx in FINGERTIPS:
        tip = landmarks[idx]
        d = ((tip.x - palm_x) ** 2 + (tip.y - palm_y) ** 2) ** 0.5
        w = FINGERTIP_WEIGHTS[idx]

        weighted_sum += w * d
        weight_total += w

    # Raw hand openness as weighted average distance from palm center
    openness_raw = weighted_sum / weight_total 

    # Calibrate based on observed min/max values (these may need adjustment)
    openness = normalize(openness_raw, min_val=MIN_OPENNESS, max_val=MAX_OPENNESS)


    # Più l'esponente è alto (es. 2.2 o 2.5), più dovrai aprire la mano 
    # verso la fine per raggiungere il 100% dell'effetto.
    openness_shaped = openness ** 2.5


    return openness_shaped

def compute_thumb_value(landmarks):
    # Thumb tip and intermediate joint
    thumb_tip = landmarks[THUMB_TIP]
    thumb_ip = landmarks[THUMB_IP]

    # Index base (stable reference on palm)
    index_base = landmarks[INDEX_MCP]

    # Wrist used to normalize for hand size
    wrist = landmarks[0]

    # Compute midpoint between thumb tip and thumb IP
    thumb_mid_x = (thumb_tip.x + thumb_ip.x) / 2
    thumb_mid_y = (thumb_tip.y + thumb_ip.y) / 2

    # Distance between thumb midpoint and index base
    d_thumb = ((thumb_mid_x - index_base.x) ** 2 +
               (thumb_mid_y - index_base.y) ** 2) ** 0.5

    # Reference distance for scale normalization (hand size)
    d_ref = ((wrist.x - index_base.x) ** 2 +
             (wrist.y - index_base.y) ** 2) ** 0.5

    # Normalize relative to hand size
    thumb_raw = d_thumb / d_ref

    # Clamp
    thumb_raw = max(MIN_THUMB_DIST, min(MAX_THUMB_DIST, thumb_raw))

    # Normalize to 0–1
    thumb_norm = (thumb_raw - MIN_THUMB_DIST) / (MAX_THUMB_DIST - MIN_THUMB_DIST)

    # Shape response (fast towards extremes)
    thumb_shaped = thumb_norm ** 1.8

    return thumb_shaped