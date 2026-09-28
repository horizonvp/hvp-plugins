"""One-off used while building v1.0.0: rename the stock face material from its Lenovo name.

Run headless from the repo root:
  UnrealEditor-Cmd.exe HVPHost\\HVPHost.uproject -ExecutePythonScript=HVPHost\\Scripts\\rename_face_material.py -nohmd -unattended

Rename through the asset tools so the object inside the package is renamed too (a plain package
redirect only renames the package). Referencers are loaded and resaved so they point at the new
name, then the redirector left behind is deleted.
"""
import unreal

OLD = "/HVPStereoButton/LenovoButtonFace_M"
NEW = "/HVPStereoButton/StereoButtonFace_M"
FOLDER = "/HVPStereoButton"

lib = unreal.EditorAssetLibrary

if lib.does_asset_exist(OLD):
    if not lib.rename_asset(OLD, NEW):
        raise SystemExit("rename failed")
    unreal.log("renamed %s -> %s" % (OLD, NEW))
else:
    unreal.log("%s not present; nothing to rename" % OLD)

# Loading every asset in the folder resolves references through the redirector; saving writes
# the resolved (new) path. only_if_is_dirty=False forces the save.
for path in lib.list_assets(FOLDER, recursive=True, include_folder=False):
    asset_path = path.split(".")[0]
    data = lib.find_asset_data(asset_path)
    if data.asset_class_path.asset_name == "ObjectRedirector":
        continue
    lib.load_asset(asset_path)
    lib.save_asset(asset_path, only_if_is_dirty=False)
    unreal.log("saved %s" % asset_path)

if lib.does_asset_exist(OLD):
    data = lib.find_asset_data(OLD)
    if data.asset_class_path.asset_name == "ObjectRedirector":
        lib.delete_asset(OLD)
        unreal.log("deleted redirector %s" % OLD)

unreal.log("done")
