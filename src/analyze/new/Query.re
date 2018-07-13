
open SharedTypes;

/* TODO maybe keep track of the "current module path" */
type queryEnv = {
  file,
  exported: Module.exported,
};

let hashFind = (tbl, key) => switch (Hashtbl.find(tbl, key)) {
  | exception Not_found => None
  | result => Some(result)
};

let rec joinPaths = (modulePath, path) => {
  switch modulePath {
    | Path.Pident({stamp, name}) => (stamp, name, path)
    | Path.Papply(fnPath, _argPath) => joinPaths(fnPath, path)
    | Path.Pdot(inner, name, _) => joinPaths(inner, Nested(name, path))
  }
};

let rec makePath = (modulePath) => {
  switch modulePath {
    | Path.Pident({stamp, name}) => `Stamp(stamp)
    | Path.Papply(fnPath, _argPath) => makePath(fnPath)
    | Path.Pdot(inner, name, _) => `Path(joinPaths(inner, Tip(name)))
  }
};


let rec resolvePathInner = (~env, ~path) => {
  switch path {
    | Tip(name) => Some(`Local(env, name))
    | Nested(subName, subPath) => {
      let%opt stamp = hashFind(env.exported.modules, subName);
      let%opt {contents: {kind}} = hashFind(env.file.stamps.modules, stamp);
      findInModule(~env, kind, subPath)
    }
  }
} and findInModule = (~env, kind, path) => {
  switch kind {
  | Module.Structure({exported}) => resolvePathInner(~env={...env, exported}, ~path)
  | Ident(modulePath, _ident) =>
    let (stamp, moduleName, fullPath) = joinPaths(modulePath, path);
    if (stamp == 0) {
      Some(`Global(moduleName, fullPath))
    } else {
      let%opt {contents: {kind}} = hashFind(env.file.stamps.modules, stamp);
      findInModule(~env, kind, fullPath);
    }
  }
};

let rec resolvePath = (~env, ~path, ~getModule) => {
  let%opt result = resolvePathInner(~env, ~path);
  switch result {
    | `Local(env, name) => Some((env, name))
    | `Global(moduleName, fullPath) => {
      let%opt file = getModule(moduleName);
      resolvePath(~env={file, exported: file.contents.exported}, ~path=fullPath, ~getModule)
    }
  }
};

open Infix;

let fromCompilerPath = (~env, path) => {
  switch (makePath(path)) {
    | `Stamp(stamp) => `Stamp(stamp)
    | `Path((0, moduleName, path)) => `Global(moduleName, path)
    | `Path((stamp, moduleName, path)) => {
      let res = {
        let%opt {contents: {Module.kind}} = hashFind(env.file.stamps.modules, stamp);
        let%opt_wrap res = findInModule(~env, kind, path);
        switch res {
          | `Local(env, name) => `Exported(env, name)
          | `Global(moduleName, fullPath) => `Global(moduleName, fullPath)
        };
      };
      res |? `Not_found
    }
  };
};

let resolveFromCompilerPath = (~env, ~getModule, path) => {
  switch (fromCompilerPath(~env, path)) {
    | `Global(moduleName, path) => {
      let res = {
        let%opt file = getModule(moduleName);
        let env = {file, exported: file.contents.exported};
        let%opt_wrap (env, name) = resolvePath(~env, ~getModule, ~path);
        `Exported(env, name)
      };
      res |? `Not_found
    }
    | `Stamp(stamp) => `Stamp(stamp)
    | `Not_found => `Not_found
    | `Exported(env, name) => `Exported(env, name)
  }
};