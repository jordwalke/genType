open DeadCommon;

let rec collect_export =
        (~mod_type=false, ~path, ~moduleName, si: Types.signature_item) =>
  switch (si) {
  | Sig_value(id, {Types.val_loc}) when !val_loc.Location.loc_ghost =>
    export(~path, ~moduleName, ~decs=valueDecs, ~id, ~loc=val_loc)

  | Sig_type(id, t, _) =>
    DeadType.collect_export([id, ...path], moduleName, t)

  | (
      Sig_module(id, {Types.md_type: moduleType, _}, _) |
      Sig_modtype(id, {Types.mtd_type: Some(moduleType), _})
    ) as s =>
    let collect =
      switch (s) {
      | Sig_modtype(_) => mod_type
      | _ => true
      };
    if (collect) {
      DeadMod.getSignature(moduleType)
      |> List.iter(
           collect_export(~mod_type, ~path=[id, ...path], ~moduleName),
         );
    };
  | _ => ()
  };

let collectValueBinding = (super, self, vb: Typedtree.value_binding) => {
  let oldPos = currentBindingPos^;
  let pos =
    switch (vb.vb_pat.pat_desc) {
    | Tpat_var(id, loc) => loc.loc.loc_start
    | _ => vb.vb_loc.loc_start
    };
  currentBindingPos := pos;
  let r = super.Tast_mapper.value_binding(self, vb);
  currentBindingPos := oldPos;
  r;
};

let collectExpr = (super, self, e: Typedtree.expression) => {
  let posUsage = e.exp_loc.loc_start;
  open Ident;
  switch (e.exp_desc) {
  | Texp_ident(
      path,
      _,
      {
        Types.val_loc: {
          Location.loc_start: posDeclaration,
          loc_ghost: false,
          _,
        },
        _,
      },
    ) =>
    addValueReference(~posDeclaration, ~posUsage)

  | Texp_field(
      _,
      x,
      {
        lbl_loc: {Location.loc_start: posDeclaration, loc_ghost: false, _},
        _,
      },
    )
  | Texp_construct(
      x,
      {
        cstr_loc: {Location.loc_start: posDeclaration, loc_ghost: false, _},
        _,
      },
      _,
    ) =>
    DeadType.collect_references(~posDeclaration, ~posUsage)

  | _ => ()
  };
  super.Tast_mapper.expr(self, e);
};

/* Traverse the AST */
let collectReferences = {
  /* Tast_mapper */
  let super = Tast_mapper.default;
  let wrap = (f, ~getPos, ~self, x) => {
    let last = lastPos^;
    let thisPos = getPos(x);
    if (thisPos != Lexing.dummy_pos) {
      lastPos := thisPos;
    };
    let r = f(super, self, x);
    lastPos := last;
    r;
  };

  let expr = (self, e) =>
    e |> wrap(collectExpr, ~getPos=x => x.exp_loc.loc_start, ~self);
  let value_binding = (self, vb) =>
    vb
    |> wrap(
         collectValueBinding,
         ~getPos=x => x.vb_expr.exp_loc.loc_start,
         ~self,
       );
  Tast_mapper.{...super, expr, value_binding};
};

let isImplementation = fn => fn.[String.length(fn) - 1] != 'i';

/* Merge a location's references to another one's */
let assoc = ((pos1, pos2)) => {
  let fn1 = pos1.Lexing.pos_fname
  and fn2 = pos2.Lexing.pos_fname;
  let hasInterface = fn =>
    switch (isImplementation(fn)) {
    | false => true
    | true =>
      getModuleName(fn) == getModuleName(currentSrc^)
      && Sys.file_exists(fn ++ "i")
    };
  let isInterface = (fn, pos) =>
    Hashtbl.mem(valueDecs, pos)
    || getModuleName(fn) != getModuleName(currentSrc^)
    || !(isImplementation(fn) && hasInterface(fn));

  if (fn1 != none_ && fn2 != none_ && pos1 != pos2) {
    if (fn1 != fn2 && isImplementation(fn1) && isImplementation(fn2)) {
      PosHash.mergeSet(valueReferences, pos2, pos1);
    };
    if (isInterface(fn1, pos1)) {
      PosHash.mergeSet(valueReferences, pos1, pos2);
      if (isInterface(fn2, pos2)) {
        addValueReference(pos1, pos2);
      };
    } else {
      PosHash.mergeSet(valueReferences, pos2, pos1);
    };
  };
};

let process_signature = (fn, signature: Types.signature) => {
  let moduleName = getModuleName(fn);
  let module_id = Ident.create(String.capitalize_ascii(moduleName));
  signature
  |> List.iter(sig_item =>
       collect_export(~path=[module_id], ~moduleName, sig_item)
     );
  lastPos := Lexing.dummy_pos;
};

let processStructure =
    (~cmtiExists, cmt_value_dependencies, structure: Typedtree.structure) => {
  structure
  |> collectReferences.Tast_mapper.structure(collectReferences)
  |> ignore;

  let posDependencies =
    cmt_value_dependencies
    |> List.rev_map(((vd1, vd2)) =>
         (
           vd1.Types.val_loc.Location.loc_start,
           vd2.Types.val_loc.Location.loc_start,
         )
       );
  posDependencies |> List.iter(assoc);
  if (cmtiExists) {
    let clean = pos => {
      let fn = pos.Lexing.pos_fname;
      if (isImplementation(fn)
          && getModuleName(fn) == getModuleName(currentSrc^)) {
        PosHash.remove(valueReferences, pos);
      };
    };
    posDependencies
    |> List.iter(((pos1, pos2)) => {
         clean(pos1);
         clean(pos2);
       });
  };
};