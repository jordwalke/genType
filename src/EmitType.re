open GenTypeCommon;

let flowExpectedError = "// $FlowExpectedError: Reason checked type sufficiently\n";

let fileHeader = (~config) =>
  switch (config.language) {
  | Flow =>
    let strictness = "strict";
    "/** \n * @flow "
    ++ strictness
    ++ "\n * @"
    ++ "generated\n * @nolint\n */\n"
    ++ "/* eslint-disable */\n";
  | TypeScript => "/* TypeScript file generated by genType. */\n"
  | Untyped => "/* Untyped file generated by genType. */\n"
  };

let generatedFilesExtension = (~config) =>
  switch (config.generatedFileExtension) {
  | Some(s) => s
  | None => ".gen"
  };

let outputFileSuffix = (~config) =>
  switch (config.language) {
  | Flow
  | Untyped => generatedFilesExtension(~config) ++ ".js"
  | TypeScript => generatedFilesExtension(~config) ++ ".tsx"
  };

let generatedModuleExtension = (~config) => generatedFilesExtension(~config);

let shimExtension = (~config) =>
  switch (config.language) {
  | Flow => ".shim.js"
  | TypeScript => ".shim.ts"
  | Untyped => ".shim.not.used"
  };

let genericsString = (~typeVars) =>
  typeVars === [] ? "" : "<" ++ String.concat(",", typeVars) ++ ">";

let interfaceName = (~config, name) =>
  config.exportInterfaces ? "I" ++ name : name;

let rec renderType =
        (~config, ~indent=None, ~typeNameIsInterface, ~inFunType, typ) =>
  switch (typ) {
  | Array(typ, arrayKind) =>
    let typIsSimple =
      switch (typ) {
      | Ident(_)
      | TypeVar(_) => true
      | _ => false
      };

    if (config.language == TypeScript && typIsSimple && arrayKind == Mutable) {
      (typ |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType))
      ++ "[]";
    } else {
      let arrayName =
        arrayKind == Mutable ?
          "Array" :
          config.language == Flow ? "$ReadOnlyArray" : "ReadonlyArray";
      arrayName
      ++ "<"
      ++ (
        typ |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType)
      )
      ++ ">";
    };

  | Function({argTypes, retType, typeVars}) =>
    renderFunType(
      ~config,
      ~indent,
      ~inFunType,
      ~typeNameIsInterface,
      ~typeVars,
      argTypes,
      retType,
    )

  | GroupOfLabeledArgs(fields)
  | Object(_, fields)
  | Record(fields) =>
    let indent1 = fields |> Indent.heuristicFields(~indent);
    let config =
      switch (typ) {
      | GroupOfLabeledArgs(_) => {...config, exportInterfaces: false}
      | _ => config
      };
    let closedFlag =
      switch (typ) {
      | Object(closedFlag, _) => closedFlag
      | _ => Closed
      };
    fields
    |> renderFields(
         ~closedFlag,
         ~config,
         ~indent=indent1,
         ~inFunType,
         ~typeNameIsInterface,
       );

  | Ident(identPath, typeArguments) =>
    (
      config.exportInterfaces && identPath |> typeNameIsInterface ?
        identPath |> interfaceName(~config) : identPath
    )
    ++ genericsString(
         ~typeVars=
           typeArguments
           |> List.map(
                renderType(
                  ~config,
                  ~indent,
                  ~typeNameIsInterface,
                  ~inFunType,
                ),
              ),
       )
  | Nullable(typ)
  | Option(typ) =>
    switch (config.language) {
    | Flow
    | Untyped =>
      "?"
      ++ (
        typ |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType)
      )
    | TypeScript =>
      "(null | undefined | "
      ++ (
        typ |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType)
      )
      ++ ")"
    }
  | Tuple(innerTypes) =>
    "["
    ++ (
      innerTypes
      |> List.map(
           renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType),
         )
      |> String.concat(", ")
    )
    ++ "]"
  | TypeVar(s) => s

  | Variant({noPayloads, payloads, unboxed, _}) =>
    let noPayloadsRendered =
      noPayloads |> List.map(case => case.labelJS |> labelJSToString);
    let field = (~name, value) => {
      mutable_: Mutable,
      name,
      optional: Mandatory,
      type_: TypeVar(value),
    };
    let fields = fields =>
      fields
      |> renderFields(
           ~closedFlag=Closed,
           ~config,
           ~indent,
           ~inFunType,
           ~typeNameIsInterface,
         );
    let payloadsRendered =
      payloads
      |> List.map(((case, _numArgs, typ)) => {
           let typRendered =
             typ
             |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType);
           unboxed ?
             typRendered :
             [
               case.labelJS
               |> labelJSToString
               |> field(~name=Runtime.jsVariantTag),
               typRendered |> field(~name=Runtime.jsVariantValue),
             ]
             |> fields;
         });
    let rendered = noPayloadsRendered @ payloadsRendered;
    let indent1 = rendered |> Indent.heuristicVariants(~indent);
    (indent1 == None ? rendered : ["", ...rendered])
    |> String.concat(
         (indent1 == None ? " " : Indent.break(~indent=indent1)) ++ "| ",
       );
  }
and renderField =
    (
      ~config,
      ~indent,
      ~typeNameIsInterface,
      ~inFunType,
      {mutable_, name: lbl, optional, type_},
    ) => {
  let optMarker = optional === Optional ? "?" : "";
  let mutMarker =
    mutable_ == Immutable ? config.language == Flow ? "+" : "readonly " : "";
  Indent.break(~indent)
  ++ mutMarker
  ++ lbl
  ++ optMarker
  ++ ": "
  ++ (type_ |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType));
}
and renderFields =
    (~closedFlag, ~config, ~indent, ~inFunType, ~typeNameIsInterface, fields) => {
  let indent1 = indent |> Indent.more;
  let exact =
    config.language == Flow
    && !config.exportInterfaces
    && fields != []
    && closedFlag == Closed;
  let space = indent == None && fields != [] ? " " : "";
  ((exact ? "{|" : "{") ++ space)
  ++ String.concat(
       config.language == TypeScript ? "; " : ", ",
       List.map(
         renderField(
           ~config,
           ~indent=indent1,
           ~typeNameIsInterface,
           ~inFunType,
         ),
         fields,
       ),
     )
  ++ Indent.break(~indent)
  ++ space
  ++ (exact ? "|}" : "}");
}
and renderFunType =
    (
      ~config,
      ~indent,
      ~inFunType,
      ~typeNameIsInterface,
      ~typeVars,
      argTypes,
      retType,
    ) =>
  (inFunType ? "(" : "")
  ++ genericsString(~typeVars)
  ++ "("
  ++ String.concat(
       ", ",
       List.mapi(
         (i, t) => {
           let parameterName =
             config.language == Flow ?
               "" : "_" ++ string_of_int(i + 1) ++ ":";
           parameterName
           ++ (
             t
             |> renderType(
                  ~config,
                  ~indent,
                  ~typeNameIsInterface,
                  ~inFunType=true,
                )
           );
         },
         argTypes,
       ),
     )
  ++ ") => "
  ++ (
    retType |> renderType(~config, ~indent, ~typeNameIsInterface, ~inFunType)
  )
  ++ (inFunType ? ")" : "");

let typeToString = (~config, ~typeNameIsInterface, typ) =>
  typ |> renderType(~config, ~typeNameIsInterface, ~inFunType=false);

let ofType = (~config, ~typeNameIsInterface, ~type_, s) =>
  config.language == Untyped ?
    s : s ++ ": " ++ (type_ |> typeToString(~config, ~typeNameIsInterface));

let emitExportConst_ =
    (
      ~early,
      ~comment="",
      ~config,
      ~emitters,
      ~name,
      ~type_,
      ~typeNameIsInterface,
      line,
    ) =>
  (comment == "" ? comment : "// " ++ comment ++ "\n")
  ++ (
    switch (config.module_, config.language) {
    | (_, TypeScript)
    | (ES6, _) =>
      "export const "
      ++ (name |> ofType(~config, ~typeNameIsInterface, ~type_))
      ++ " = "
      ++ line
    | (CommonJS, _) =>
      "const "
      ++ (name |> ofType(~config, ~typeNameIsInterface, ~type_))
      ++ " = "
      ++ line
      ++ ";\nexports."
      ++ name
      ++ " = "
      ++ name
    }
  )
  |> (early ? Emitters.exportEarly : Emitters.export)(~emitters);

let emitExportConst = emitExportConst_(~early=false);

let emitExportConstEarly = emitExportConst_(~early=true);

let emitExportConstMany =
    (~config, ~emitters, ~name, ~type_, ~typeNameIsInterface, lines) =>
  lines
  |> String.concat("\n")
  |> emitExportConst(~config, ~emitters, ~name, ~type_, ~typeNameIsInterface);

let emitExportFunction =
    (~early, ~comment="", ~emitters, ~name, ~config, line) =>
  (comment == "" ? comment : "// " ++ comment ++ "\n")
  ++ (
    switch (config.module_, config.language) {
    | (_, TypeScript)
    | (ES6, _) => "export function " ++ name ++ line
    | (CommonJS, _) =>
      "function " ++ name ++ line ++ ";\nexports." ++ name ++ " = " ++ name
    }
  )
  |> (early ? Emitters.exportEarly : Emitters.export)(~emitters);

let emitExportDefault = (~emitters, ~config, name) =>
  switch (config.module_, config.language) {
  | (_, TypeScript)
  | (ES6, _) =>
    "export default " ++ name ++ ";" |> Emitters.export(~emitters)
  | (CommonJS, _) =>
    "exports.default = " ++ name ++ ";" |> Emitters.export(~emitters)
  };

let emitExportType =
    (
      ~early=false,
      ~config,
      ~emitters,
      ~nameAs,
      ~opaque,
      ~optType,
      ~typeNameIsInterface,
      ~typeVars,
      resolvedTypeName,
    ) => {
  let export = early ? Emitters.exportEarly : Emitters.export;
  let typeParamsString = genericsString(~typeVars);
  let isInterface = resolvedTypeName |> typeNameIsInterface;
  let resolvedTypeName =
    config.exportInterfaces && isInterface ?
      resolvedTypeName |> interfaceName(~config) : resolvedTypeName;
  let exportNameAs =
    switch (nameAs) {
    | None => ""
    | Some(s) => "\nexport type " ++ s ++ " = " ++ resolvedTypeName ++ ";"
    };

  switch (config.language) {
  | Flow =>
    switch (optType) {
    | Some(typ) when config.exportInterfaces && isInterface && !opaque =>
      "export interface "
      ++ resolvedTypeName
      ++ typeParamsString
      ++ " "
      ++ (
        (opaque ? mixedOrUnknown(~config) : typ)
        |> typeToString(~config, ~typeNameIsInterface)
      )
      ++ ";"
      ++ exportNameAs
      |> export(~emitters)
    | Some(typ) =>
      "export"
      ++ (opaque ? " opaque " : " ")
      ++ "type "
      ++ resolvedTypeName
      ++ typeParamsString
      ++ " = "
      ++ (
        (opaque ? mixedOrUnknown(~config) : typ)
        |> typeToString(~config, ~typeNameIsInterface)
      )
      ++ ";"
      ++ exportNameAs
      |> export(~emitters)
    | None =>
      "export"
      ++ (opaque ? " opaque " : " ")
      ++ "type "
      ++ (resolvedTypeName |> EmitText.brackets)
      ++ ";"
      ++ exportNameAs
      |> export(~emitters)
    }
  | TypeScript =>
    if (opaque) {
      /* Represent an opaque type as an absract class with a field called 'opaque'.
         Any type parameters must occur in the type of opaque, so that different
         instantiations are considered different types. */
      let typeOfOpaqueField =
        typeVars == [] ? "any" : typeVars |> String.concat(" | ");
      "// tslint:disable-next-line:max-classes-per-file \n"
      ++ (
        String.capitalize(resolvedTypeName) != resolvedTypeName ?
          "// tslint:disable-next-line:class-name\n" : ""
      )
      ++ "export abstract class "
      ++ resolvedTypeName
      ++ typeParamsString
      ++ " { protected opaque!: "
      ++ typeOfOpaqueField
      ++ " }; /* simulate opaque types */"
      ++ exportNameAs
      |> export(~emitters);
    } else {
      (
        if (isInterface && config.exportInterfaces) {
          "export interface " ++ resolvedTypeName ++ typeParamsString ++ " ";
        } else {
          "// tslint:disable-next-line:interface-over-type-literal\n"
          ++ "export type "
          ++ resolvedTypeName
          ++ typeParamsString
          ++ " = ";
        }
      )
      ++ (
        switch (optType) {
        | Some(typ) => typ |> typeToString(~config, ~typeNameIsInterface)
        | None => resolvedTypeName
        }
      )
      ++ ";"
      ++ exportNameAs
      |> export(~emitters);
    }
  | Untyped => emitters
  };
};

let emitImportValueAsEarly = (~config, ~emitters, ~name, ~nameAs, importPath) => {
  let commentBeforeImport =
    config.language == Flow ?
      "// flowlint-next-line nonstrict-import:off\n" : "";
  commentBeforeImport
  ++ "import "
  ++ (
    switch (nameAs) {
    | Some(nameAs) => "{" ++ name ++ " as " ++ nameAs ++ "}"
    | None => name
    }
  )
  ++ " from "
  ++ "'"
  ++ (importPath |> ImportPath.toString)
  ++ "';"
  |> Emitters.requireEarly(~emitters);
};

let emitRequire =
    (
      ~importedValueOrComponent,
      ~early,
      ~emitters,
      ~config,
      ~moduleName,
      ~strict,
      importPath,
    ) => {
  let commentBeforeRequire =
    switch (config.language) {
    | TypeScript => "// tslint:disable-next-line:no-var-requires\n"
    | Flow =>
      strict ?
        early ? "// flowlint-next-line nonstrict-import:off\n" : "" :
        flowExpectedError
    | Untyped => ""
    };
  switch (config.module_) {
  | ES6 when !importedValueOrComponent && config.language != TypeScript =>
    commentBeforeRequire
    ++ "import * as "
    ++ ModuleName.toString(moduleName)
    ++ " from '"
    ++ (importPath |> ImportPath.toString)
    ++ "';"
    |> (early ? Emitters.requireEarly : Emitters.require)(~emitters)
  | _ =>
    commentBeforeRequire
    ++ "const "
    ++ ModuleName.toString(moduleName)
    ++ " = require('"
    ++ (importPath |> ImportPath.toString)
    ++ "');"
    |> (early ? Emitters.requireEarly : Emitters.require)(~emitters)
  };
};

let require = (~early) => early ? Emitters.requireEarly : Emitters.require;

let emitRequireReact = (~early, ~emitters, ~config) =>
  switch (config.language) {
  | Flow
  | Untyped =>
    emitRequire(
      ~importedValueOrComponent=false,
      ~early,
      ~emitters,
      ~config,
      ~moduleName=ModuleName.react,
      ~strict=false,
      ImportPath.react,
    )
  | TypeScript =>
    "import * as React from 'react';" |> require(~early, ~emitters)
  };

let reactComponentType = (~config, ~propsTypeName) =>
  Ident(
    config.language == Flow ? "React$ComponentType" : "React.ComponentClass",
    [Ident(propsTypeName, [])],
  );

let componentExportName = (~config, ~fileName, ~moduleName) =>
  switch (config.language) {
  | Flow =>
    fileName == moduleName ? "component" : moduleName |> ModuleName.toString
  | _ => moduleName |> ModuleName.toString
  };

let emitImportTypeAs =
    (
      ~emitters,
      ~config,
      ~typeName,
      ~asTypeName,
      ~typeNameIsInterface,
      ~importPath,
    ) => {
  let (typeName, asTypeName) =
    switch (asTypeName) {
    | Some(asName) =>
      asName |> typeNameIsInterface ?
        (
          typeName |> interfaceName(~config),
          Some(asName |> interfaceName(~config)),
        ) :
        (typeName, asTypeName)
    | None => (typeName, asTypeName)
    };
  let strictLocalPrefix =
    config.language == Flow ?
      "// flowlint-next-line nonstrict-import:off\n" : "";
  switch (config.language) {
  | Flow
  | TypeScript =>
    strictLocalPrefix
    ++ "import "
    ++ (config.language == Flow ? "type " : "")
    ++ "{"
    ++ typeName
    ++ (
      switch (asTypeName) {
      | Some(asT) => " as " ++ asT
      | None => ""
      }
    )
    ++ "} from '"
    ++ (importPath |> ImportPath.toString)
    ++ "';"
    |> Emitters.import(~emitters)
  | Untyped => emitters
  };
};

let ofTypeAnyTS = (~config, s) =>
  config.language == TypeScript ? s ++ ": any" : s;

let emitTypeCast = (~config, ~type_, ~typeNameIsInterface, s) =>
  switch (config.language) {
  | TypeScript =>
    s ++ " as " ++ (type_ |> typeToString(~config, ~typeNameIsInterface))
  | Untyped
  | Flow => s
  };