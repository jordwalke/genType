/***************************************************************************/
/*                                                                         */
/*   Copyright (c) 2014-2016 LexiFi SAS. All rights reserved.              */
/*                                                                         */
/*   This source code is licensed under the MIT License                    */
/*   found in the LICENSE file at the root of this source tree             */
/*                                                                         */
/***************************************************************************/

let reportUnderscore = ref(false);

let active = Sys.getenv_opt("Global") != None;

let transitive = Sys.getenv_opt("Transitive") != None;

let write = Sys.getenv_opt("Write") != None;

let verbose = Sys.getenv_opt("Verbose") != None;

let deadAnnotation = "dead";

/* Location printer: `filename:line: ' */
let posToString = (~printCol=false, ~shortFile=false, pos: Lexing.position) => {
  let file = pos.Lexing.pos_fname;
  let line = pos.Lexing.pos_lnum;
  let col = pos.Lexing.pos_cnum - pos.Lexing.pos_bol;
  (shortFile ? file |> Filename.basename : file)
  ++ ":"
  ++ string_of_int(line)
  ++ (printCol ? ":" ++ string_of_int(col) : ": ");
};

/********   ATTRIBUTES   ********/
module PosSet =
  Set.Make({
    type t = Lexing.position;
    let compare = compare;
  });

module PosHash = {
  include Hashtbl.Make({
    type t = Lexing.position;

    let hash = x => {
      let s = Filename.basename(x.Lexing.pos_fname);
      Hashtbl.hash((x.Lexing.pos_cnum, s));
    };

    let equal = (x: t, y) => x == y;
  });

  let findSet = (h, k) =>
    try(find(h, k)) {
    | Not_found => PosSet.empty
    };

  let addSet = (h, k, v) => {
    let set = findSet(h, k);
    replace(h, k, PosSet.add(v, set));
  };

  let mergeSet = (table, pos1, pos2) => {
    if (verbose) {
      GenTypeCommon.logItem(
        "mergeSet %s <- %s\n",
        pos1 |> posToString(~printCol=true, ~shortFile=true),
        pos2 |> posToString(~printCol=true, ~shortFile=true),
      );
    };

    let set1 = findSet(table, pos1);
    let set2 = findSet(table, pos2);
    replace(table, pos1, PosSet.union(set1, set2));
  };
};

type decs = Hashtbl.t(Lexing.position, string);
let valueDecs: decs = Hashtbl.create(256); /* all exported value declarations */
let typeDecs: decs = Hashtbl.create(256);

let references: PosHash.t(PosSet.t) = (
  PosHash.create(256): PosHash.t(PosSet.t)
); /* all value references */

let fields: Hashtbl.t(string, Lexing.position) = (
  Hashtbl.create(256): Hashtbl.t(string, Lexing.position)
); /* link from fields (record/variant) paths and locations */

let lastPos = ref(Lexing.dummy_pos); /* helper to diagnose occurrences of Location.none in the typedtree */
let currentSrc = ref("");
let currentBindingPos = ref(Lexing.dummy_pos);

let mods: ref(list(string)) = (ref([]): ref(list(string))); /* module path */

let none_ = "_none_";
let include_ = "*include*";

/********   HELPERS   ********/

let addReference = (~posDeclaration, ~posUsage) => {
  let posUsage =
    !transitive || currentBindingPos^ == Lexing.dummy_pos
      ? posUsage : currentBindingPos^;
  if (verbose) {
    GenTypeCommon.logItem(
      "addReference declaration:%s  usage:%s\n",
      posDeclaration |> posToString(~printCol=true, ~shortFile=true),
      posUsage |> posToString(~printCol=true, ~shortFile=true),
    );
  };
  PosHash.addSet(references, posDeclaration, posUsage);
};

let getModuleName = fn => fn |> Paths.getModuleName |> ModuleName.toString;

let check_underscore = name => reportUnderscore^ || name.[0] != '_';

let hashtbl_add_to_list = (hashtbl, key, elt) =>
  Hashtbl.add(hashtbl, key, elt);

/********   PROCESSING  ********/

let export = (path, u, stock: decs, id, loc) => {
  let value =
    String.concat(".", List.rev_map(Ident.name, path))
    ++ "."
    ++ id.Ident.name;

  /* a .cmi file can contain locations from other files.
       For instance:
           module M : Set.S with type elt = int
       will create value definitions whose location is in set.mli
     */
  if (!loc.Location.loc_ghost
      && (
        u == getModuleName(loc.Location.loc_start.Lexing.pos_fname)
        || u === include_
      )
      && check_underscore(id.Ident.name)) {
    hashtbl_add_to_list(stock, loc.Location.loc_start, value);
  };
};

/**** REPORTING ****/

/* Faster than 'List.length l = len' when len < List.length l; same speed otherwise*/
let rec check_length = len =>
  fun
  | [] => len == 0
  | [_, ...l] when len > 0 => check_length(len - 1, l)
  | _ => false;

let pathWithoutHead = path => {
  let rec cutFromNextDot = (s, pos) =>
    if (pos == String.length(s)) {
      s;
    } else if (s.[pos] == '.') {
      String.sub(s, pos + 1, String.length(s) - pos - 1);
    } else {
      cutFromNextDot(s, pos + 1);
    };
  cutFromNextDot(path, 0);
};

/* Keep track of the location of values exported via genType */
module ProcessAnnotations = {
  /* Positions exported to JS */
  let positionsAnnotatedWithGenType = PosHash.create(1);
  let positionsAnnotatedDead = PosHash.create(1);

  let isAnnotatedDead = pos => PosHash.mem(positionsAnnotatedDead, pos);

  let isAnnotatedGentypeOrDead = pos =>
    PosHash.mem(positionsAnnotatedWithGenType, pos) || isAnnotatedDead(pos);

  let posAnnotatedWithGenType = (pos: Lexing.position) => {
    PosHash.replace(positionsAnnotatedWithGenType, pos, ());
  };

  let posAnnotatedDead = (pos: Lexing.position) => {
    PosHash.replace(positionsAnnotatedDead, pos, ());
  };

  let processAttributes = (~ignoreInterface, ~pos, attributes) => {
    if (attributes |> Annotation.hasGenTypeAnnotation(~ignoreInterface)) {
      pos |> posAnnotatedWithGenType;
    };
    if (attributes
        |> Annotation.getAttributePayload((==)(deadAnnotation)) != None) {
      pos |> posAnnotatedDead;
    };
  };

  let collectExportLocations = (~ignoreInterface) => {
    let super = Tast_mapper.default;
    let value_binding =
        (
          self,
          {vb_attributes, vb_pat} as value_binding: Typedtree.value_binding,
        ) => {
      switch (vb_pat.pat_desc) {
      | Tpat_var(id, pLoc) =>
        vb_attributes
        |> processAttributes(~ignoreInterface, ~pos=pLoc.loc.loc_start)

      | _ => ()
      };
      super.value_binding(self, value_binding);
    };
    let value_description =
        (
          self,
          {val_attributes, val_id, val_val} as value_description: Typedtree.value_description,
        ) => {
      val_attributes
      |> processAttributes(~ignoreInterface, ~pos=val_val.val_loc.loc_start);
      super.value_description(self, value_description);
    };
    {...super, value_binding, value_description};
  };

  let structure = structure => {
    let ignoreInterface = ref(false);
    let collectExportLocations = collectExportLocations(~ignoreInterface);
    structure
    |> collectExportLocations.structure(collectExportLocations)
    |> ignore;
  };
  let signature = signature => {
    let ignoreInterface = ref(false);
    let collectExportLocations = collectExportLocations(~ignoreInterface);
    signature
    |> collectExportLocations.signature(collectExportLocations)
    |> ignore;
  };
};

type item = {
  pos: Lexing.position,
  path: string,
};

let compareItems = ({path: path1, pos: pos1}, {path: path2, pos: pos2}) =>
  compare((pos1, path1), (pos2, path2));

let report = (~useDead=false, ~onItem, decs: decs) => {
  let dontReportDead = pos =>
    useDead && ProcessAnnotations.isAnnotatedGentypeOrDead(pos);

  let folder = (items, {pos, path}) => {
    switch (pos |> PosHash.findSet(references)) {
    | referencesToLoc when !(pos |> dontReportDead) =>
      let liveReferences =
        referencesToLoc
        |> PosSet.filter(pos => !ProcessAnnotations.isAnnotatedDead(pos));
      if (liveReferences |> PosSet.cardinal == 0) {
        if (transitive) {
          pos |> ProcessAnnotations.posAnnotatedDead;
        };
        [{pos, path: pathWithoutHead(path)}, ...items];
      } else {
        if (verbose) {
          let refsString =
            referencesToLoc
            |> PosSet.elements
            |> List.map(posToString(~printCol=true, ~shortFile=true))
            |> String.concat(", ");
          GenTypeCommon.logItem(
            "%s: %d references (%s)\n",
            path,
            referencesToLoc |> PosSet.cardinal,
            refsString,
          );
        };
        items;
      };
    | _ => items
    | exception Not_found => items
    };
  };

  Hashtbl.fold((pos, path, items) => [{pos, path}, ...items], decs, [])
  |> List.fast_sort((i1, i2) => compareItems(i2, i1))  /* analyze in reverse order */
  |> List.fold_left(folder, [])
  |> List.iter(onItem);
};