"""
Shared map style table: OSM tags -> style id, and style id -> colour/width.

This is the single source of truth for how features look. `bake.py` stores only
the style id in each feature (1 byte); the renderer (preview.py today, the
cluster firmware later) owns the id -> (colour, width) mapping, so restyling
never requires re-baking. Colours are RGB here; the firmware will hold the same
table as RGB565.
"""

# Style ids. Roads are ordered by importance so the renderer can draw low ids
# first and let arterials sit on top.
S_MINOR = 0      # residential, service, unclassified, living_street
S_MID = 1        # secondary, tertiary
S_PRIMARY = 2    # primary
S_MAJOR = 3      # motorway, trunk
S_WATER = 10     # water fill
S_BUILDING = 11  # building footprint fill

# style id -> (RGB, stroke width px at 256 px/tile). Width is ignored for fills.
STYLE_TABLE = {
    S_MINOR:    ((0x53, 0x57, 0x5E), 1),
    S_MID:      ((0x7A, 0x80, 0x8A), 2),
    S_PRIMARY:  ((0xC9, 0xA2, 0x4B), 3),
    S_MAJOR:    ((0xE6, 0xB8, 0x4B), 4),
    S_WATER:    ((0x1E, 0x33, 0x49), 0),
    S_BUILDING: ((0x26, 0x28, 0x2F), 0),  # a touch above the dark base, blocks read subtly
}

BACKGROUND = (0x10, 0x11, 0x16)   # dark base, matches the cluster's dark theme

_ROAD_CLASS = {
    "motorway": S_MAJOR, "motorway_link": S_MAJOR,
    "trunk": S_MAJOR, "trunk_link": S_MAJOR,
    "primary": S_PRIMARY, "primary_link": S_PRIMARY,
    "secondary": S_MID, "secondary_link": S_MID,
    "tertiary": S_MID, "tertiary_link": S_MID,
    "residential": S_MINOR, "unclassified": S_MINOR,
    "living_street": S_MINOR, "service": S_MINOR, "road": S_MINOR,
}

# Pedestrian-only ways add clutter to a riding map; drop them.
_ROAD_DROP = {"footway", "path", "steps", "cycleway", "bridleway",
              "corridor", "pedestrian", "track"}


def classify(props, is_polygon):
    """Return a style id for a feature, or None to drop it."""
    if is_polygon:
        if props.get("natural") == "water" or "water" in props \
                or props.get("waterway") in ("riverbank", "dock"):
            return S_WATER
        if props.get("building"):
            return S_BUILDING
        return None
    hw = props.get("highway")
    if hw is None or hw in _ROAD_DROP:
        # waterway lines (rivers/streams) render as thin water strokes.
        if props.get("waterway") in ("river", "stream", "canal"):
            return S_WATER
        return None
    return _ROAD_CLASS.get(hw, S_MINOR)
