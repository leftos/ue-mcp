import type { ToolDef } from "../core/types.js";
import { categoryTool } from "../surface/category-tool.js";
import { specBp, schema as specSchema } from "./specs/fab.generated.js";

// Fab asset importer. Fab is Epic's unified content marketplace; UE ships an
// editor plugin whose window is a web frontend (catalog browse, purchase,
// adding a listing to the library, and producing a signed download URL all
// happen in fab.com's pages) sitting on a native download+import layer. This
// category drives the native pieces that don't need the web frontend: login
// lifecycle and sign-in state, syncing the user's owned library into the
// Content Browser, inspecting/clearing the download cache, importing local source files, and
// handing a signed download URL to the plugin's own add-to-project workflows.
//
// The plugin cannot produce a signed URL itself (the web page generates it),
// but it can use one: add_to_project passes a URL obtained elsewhere to the
// same native function the web page calls. Store catalog browsing and buying
// stay in the web window.
export const fabTool: ToolDef = categoryTool(
  "fab",
  "Import Fab (Epic marketplace) content: check plugin status and sign-in state, trigger login/logout, sync your owned library into the Content Browser, inspect/clear the download cache, import local source files, and add an owned listing to the project from a signed download URL.",
  {
    status:       specBp("read", "Report Fab plugin state: whether the module is loaded, whether the native import/cache API is linked in this build, whether the Fab window has been opened this session, and the download cache location/size. Call this first.", "fab_status"),
    login:        specBp("mutate", "Trigger the Fab login flow (EOS account portal). Asynchronous - returns once the flow is opened, not once authenticated. Complete any prompt, then call auth_state. Reports alreadySignedIn, whether an account was signed in before the flow opened, read as auth_state reads it. No inverse: it creates no state of its own, and logout would clear a session that may predate the call, so the response says rollbackPossible=false. It carries no unchanged flag, and says so with idempotencyObservable=false: whether the flow ends in a sign-in is decided in the account portal after the call returns.", "fab_login"),
    logout:       specBp("mutate", "Clear the persistent Fab authentication for this device. No inverse: logging back in needs a person at the EOS account portal, and no call restores cleared credentials. idempotencyObservable=false, because the stored credentials it clears are not readable from here (auth_state reads the session, not the store).", "fab_logout"),
    sync_library: specBp("mutate", "Load the user's owned Fab library (\"My Folder\") into the Content Browser via TEDS. Requires an active login; items appear asynchronously. No inverse: it populates an in-editor index of what the account already owns, nothing un-lists it, and nothing on disk or in the project changes. idempotencyObservable=false, because the sync lands after this call returns.", "fab_sync_library"),
    list_cached:  specBp("read", "List the entries currently in the local Fab download cache (already-downloaded owned assets).", "fab_list_cached"),
    cache_info:   specBp("read", "Report the Fab download cache location, total size, and entry count.", "fab_cache_info"),
    clear_cache:  specBp("mutate", "Delete the local Fab download cache to reclaim disk. Does not affect assets already imported into the project. Reports unchanged=true when the cache was already empty (where the native Fab API is linked). No inverse: the downloads come back only by downloading them again.", "fab_clear_cache"),
    import_file:  specBp("mutate", "Import a source file into the project through the Fab Interchange import pipeline. Use for owned assets that are downloaded/cached locally, or any local source file (fbx, textures). Single files import synchronously and report the created asset paths; pack/quixel workflows may run asynchronously. A synchronous import rolls back by deleting exactly the assets it created, taken from the paths the importer reported (a force delete, because the imported set references itself). An ASYNCHRONOUS import emits no inverse: its paths are not known yet, and a record naming the destination folder would delete whatever else already lives there.", "fab_import_file"),
    auth_state:   specBp("read", "Report whether the editor's Fab plugin holds a signed-in session, read back from the plugin itself, and the Epic account id the session names. Never returns the session token. The plugin restores a remembered sign-in only when a Fab tab opens, so an editor that never opened one reads signedIn=false.", "fab_auth_state"),
    add_to_project: specBp("mutate", "Add an owned listing to the project from a signed download URL, through the Fab plugin's own AddToProject: the pack, Megascans, glTF/FBX or MetaHuman workflow the plugin picks from assetType and isQuixel downloads and imports it. The URL comes from fab.com (the plugin cannot produce one). Reports the workflow chosen; the download and import run asynchronously, so no asset paths are known when it returns and a bad or expired URL fails in the workflow, not here. No inverse, as for import_file's asynchronous branch: the paths are not known yet, and a record naming the destination folder would delete whatever else lives there. idempotencyObservable=false, because whether the asset was already added is decided inside the workflow after this returns.", "fab_add_to_project"),
  },
  {
    // #1057: every key the fab handlers declare, generated from their C++
    // registrations.
    ...specSchema,
  },
);
