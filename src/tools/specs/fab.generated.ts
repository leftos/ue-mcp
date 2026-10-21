// GENERATED FILE - do not edit.
//
// Written by scripts/generate-handler-specs.mjs from tests/golden/handler-specs.json,
// which is recorded from the parameter specs the C++ handlers register with
// (npm run specs:record). To change a parameter, change its RegisterHandler
// spec, re-record, and regenerate (#1057).
import { z } from "zod";
import { makeSpecBp, type HandlerSpecs } from "../../surface/handler-spec.js";

/** The recorded contract of every spec'd fab handler. */
export const handlerSpecs: HandlerSpecs = {
  "fab_add_to_project": {
    "category": "fab",
    "params": [
      {
        "name": "downloadUrl",
        "type": "string",
        "required": true,
        "description": "Signed download URL for the listing (a pack's manifest URL), as fab.com issues it"
      },
      {
        "name": "assetId",
        "type": "string",
        "required": true,
        "description": "Fab asset id of the listing"
      },
      {
        "name": "assetName",
        "type": "string",
        "required": true,
        "description": "Listing name, used for the import folder and notifications"
      },
      {
        "name": "assetType",
        "type": "string",
        "required": true,
        "description": "Fab asset type: unreal-engine (a pack), gltf, glb, fbx, metahuman, or a type a plugin registered"
      },
      {
        "name": "isQuixel",
        "type": "boolean",
        "required": false,
        "description": "Megascans content; picks the Quixel workflow for any type but unreal-engine (default false)"
      },
      {
        "name": "distributionPointBaseUrls",
        "type": "array",
        "required": false,
        "description": "Chunk base URLs a pack downloads from; required for unreal-engine",
        "items": "string"
      },
      {
        "name": "listingType",
        "type": "string",
        "required": false,
        "description": "Fab listing type, passed through to the workflow"
      },
      {
        "name": "assetNamespace",
        "type": "string",
        "required": false,
        "description": "Fab asset namespace, passed through to the workflow"
      }
    ]
  },
  "fab_auth_state": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters, and the plugin's token read it makes logs an EOS error whenever no account is signed in"
  },
  "fab_cache_info": {
    "category": "fab",
    "params": []
  },
  "fab_clear_cache": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and deletes the Fab download cache when called"
  },
  "fab_import_file": {
    "category": "fab",
    "params": [
      {
        "name": "source",
        "type": "string",
        "required": true,
        "description": "Absolute path of the source file on disk",
        "aliases": [
          "sourceFile"
        ]
      },
      {
        "name": "destination",
        "type": "string",
        "required": true,
        "description": "Destination content path, e.g. /Game/Fab/Imported",
        "aliases": [
          "destPath"
        ]
      }
    ]
  },
  "fab_list_cached": {
    "category": "fab",
    "params": []
  },
  "fab_login": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and opens the EOS account-portal login flow when called"
  },
  "fab_logout": {
    "category": "fab",
    "params": [],
    "contractExempt": "Takes no parameters and clears the stored Fab credentials when called"
  },
  "fab_status": {
    "category": "fab",
    "params": []
  },
  "fab_sync_library": {
    "category": "fab",
    "params": [
      {
        "name": "batchSize",
        "type": "integer",
        "required": false,
        "description": "Library items to pull per sync request (default the plugin's own)"
      }
    ],
    "contractExempt": "Queues a Fab library sync whatever the batch size"
  }
};

/** The Params: clause of each spec'd bridge method. */
export const paramsClauses: Readonly<Record<string, string>> = {
  fab_add_to_project: "Params: downloadUrl, assetId, assetName, assetType, isQuixel?, distributionPointBaseUrls?, listingType?, assetNamespace?",
  fab_auth_state: "Params: none",
  fab_cache_info: "Params: none",
  fab_clear_cache: "Params: none",
  fab_import_file: "Params: source (or sourceFile), destination (or destPath)",
  fab_list_cached: "Params: none",
  fab_login: "Params: none",
  fab_logout: "Params: none",
  fab_status: "Params: none",
  fab_sync_library: "Params: batchSize?",
};

/** Every key the spec'd fab handlers declare, aliases included. */
export const schema: Record<string, z.ZodType> = {
  assetId: z.string().optional().describe("Fab asset id of the listing"),
  assetName: z.string().optional().describe("Listing name, used for the import folder and notifications"),
  assetNamespace: z.string().optional().describe("Fab asset namespace, passed through to the workflow"),
  assetType: z.string().optional().describe("Fab asset type: unreal-engine (a pack), gltf, glb, fbx, metahuman, or a type a plugin registered"),
  batchSize: z.number().int().optional().describe("Library items to pull per sync request (default the plugin's own)"),
  destination: z.string().optional().describe("Destination content path, e.g. /Game/Fab/Imported"),
  destPath: z.string().optional().describe("Alias for destination"),
  distributionPointBaseUrls: z.array(z.string()).optional().describe("Chunk base URLs a pack downloads from; required for unreal-engine"),
  downloadUrl: z.string().optional().describe("Signed download URL for the listing (a pack's manifest URL), as fab.com issues it"),
  isQuixel: z.boolean().optional().describe("Megascans content; picks the Quixel workflow for any type but unreal-engine (default false)"),
  listingType: z.string().optional().describe("Fab listing type, passed through to the workflow"),
  source: z.string().optional().describe("Absolute path of the source file on disk"),
  sourceFile: z.string().optional().describe("Alias for source"),
};

/** Declare an action for a spec'd bridge method: effect, summary, method. */
export const specBp = makeSpecBp(paramsClauses, handlerSpecs);
