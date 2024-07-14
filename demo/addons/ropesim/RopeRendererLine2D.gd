@tool
extends Line2D
class_name RopeRendererLine2D

const UPDATE_HOOK = "on_post_update"
const HOOK_FUNC = "refresh"

## Convenience property that can be pressed in editor to force an update.
@export var force_update: bool: set = _force_update

## Target rope.
@export_node_path("Rope") var target_rope_path: NodePath = "..": set = set_rope_path

## Update automatically. If disabled, a manual call to [method RopeRendererLine2D.refresh] is needed
## to update the line rendering.
@export var auto_update: bool = true: get = get_auto_update, set = set_auto_update

## Inverts the point order. The shape of the rope remains the same but the texture will repeat
## towards the beginning rather than the end.
## Useful when dynamically changing the rope's length.
@export var invert: bool = false

var _helper: RopeToolHelper


func _init() -> void:
    if not _helper:
        _helper = RopeToolHelper.new(RopeToolHelper.UPDATE_HOOK_POST, self, "refresh")
        add_child(_helper)


func _ready() -> void:
    set_rope_path(target_rope_path)
    set_auto_update(auto_update)
    refresh()


func refresh() -> void:
    var target: Rope = _helper.target_rope

    if target and target.get_num_points() > 0 and visible:
        var xform: Transform2D

        if keep_rope_position:
            if Engine.is_editor_hint():
                xform = Transform2D(0, -global_position - target.get_point(0) + target.global_position)
            else:
                xform = Transform2D(0, -global_position)
        else:
            xform = Transform2D(0, -target.get_point(0))

        xform = xform.scaled(scale)
        var p: PackedVector2Array = xform * target.get_points()

        if invert:
            p.reverse()

        points = p
        global_rotation = 0


func set_rope_path(value: NodePath) -> void:
    target_rope_path = value
    if is_inside_tree():
        _helper.target_rope = get_node(target_rope_path) as Rope
        refresh()


func _force_update(_value: bool) -> void:
    refresh()


func _set_keep_pos(value: bool) -> void:
    keep_rope_position = value
    refresh()


func set_auto_update(value: bool) -> void:
    _helper.enable = value

func get_auto_update() -> bool:
    return _helper.enable
