"""One-off used while building v1.0.0: point the stock face widget's text at the engine's Roboto.

The widget shipped referencing a Lenovo project font. A plugin may not reference project content,
and Roboto is present in every project. Projects put their own font on their own face widgets.
"""
import unreal

WBP = "/HVPStereoButton/StereoButtonFace_WBP"
ROBOTO = "/Engine/EngineFonts/Roboto"

bp = unreal.load_asset(WBP)
roboto = unreal.load_asset(ROBOTO)
tree = unreal.find_object(bp, "WidgetTree")
if tree is None:
    raise SystemExit("no WidgetTree subobject")

def walk(widget):
    yield widget
    if isinstance(widget, unreal.PanelWidget):
        for child in widget.get_all_children():
            for w in walk(child):
                yield w

widgets = []
try:
    widgets = list(walk(tree.get_editor_property("root_widget")))
except Exception as e:
    unreal.log_warning("root_widget not reflected (%s); falling back to known names" % e)
    for name in ("TextBlock_96",):
        w = unreal.find_object(tree, name)
        if w is not None:
            widgets.append(w)

changed = 0
for w in widgets:
    if isinstance(w, unreal.TextBlock):
        font = w.get_editor_property("font")
        unreal.log("%s font_object=%s size=%s" % (w.get_name(), font.get_editor_property("font_object"), font.get_editor_property("size")))
        if font.get_editor_property("font_object") != roboto:
            font.set_editor_property("font_object", roboto)
            w.set_font(font)
            changed += 1
            unreal.log("set font on %s (size %s)" % (w.get_name(), font.get_editor_property("size")))

unreal.BlueprintEditorLibrary.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_asset(WBP, only_if_is_dirty=False)
unreal.log("done: %d text block(s) updated of %d widgets seen" % (changed, len(widgets)))

