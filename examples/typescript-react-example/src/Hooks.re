type vehicle = {name: string};

[@dead "makeProps"] [@react.component]
let make = (~vehicle) => {
  let (count, setCount) = React.useState(() => 0);

  <div>
    <p>
      {React.string(
         "Hooks example "
         ++ vehicle.name
         ++ " clicked "
         ++ string_of_int(count)
         ++ " times",
       )}
    </p>
    <button onClick={_ => setCount(_ => count + 1)}>
      {React.string("Click me")}
    </button>
    <ImportHooks
      person={name: "Mary", age: 71}
      renderMe={x => React.string(x##randomString)}>
      {React.string("child1")}
      {React.string("child2")}
    </ImportHooks>
    <ImportHookDefault
      person={name: "DefaultImport", age: 42}
      renderMe={x => React.string(x##randomString)}>
      {React.string("child1")}
      {React.string("child2")}
    </ImportHookDefault>
  </div>;
};

[@genType]
let default = make;

[@genType]
[@dead "anotherComponentProps"] [@react.component]
let anotherComponent = (~vehicle, ~callback: unit => unit) => {
  callback();
  <div> {React.string("Another Hook " ++ vehicle.name)} </div>;
};

module Inner = {
  [@genType]
[@dead "Inner.makeProps"]   [@react.component]
  let make = (~vehicle) =>
    <div> {React.string("Another Hook " ++ vehicle.name)} </div>;

  [@genType]
[@dead "Inner.anotherComponentProps"]   [@react.component]
  let anotherComponent = (~vehicle) =>
    <div> {React.string("Another Hook " ++ vehicle.name)} </div>;

  module Inner2 = {
    [@genType]
[@dead "Inner.Inner2.makeProps"]     [@react.component]
    let make = (~vehicle) =>
      <div> {React.string("Another Hook " ++ vehicle.name)} </div>;

    [@genType]
[@dead "Inner.Inner2.anotherComponentProps"]     [@react.component]
    let anotherComponent = (~vehicle) =>
      <div> {React.string("Another Hook " ++ vehicle.name)} </div>;
  };
};

module NoProps = {
  [@genType]
[@dead "NoProps.makeProps"]   [@react.component]
  let make = () => {
    <div> ReasonReact.null </div>;
  };
};

type cb = (~_to: vehicle) => unit;

[@genType]
let functionWithRenamedArgs = (~_to, ~_Type, ~cb: cb) => {
  cb(~_to);
  _to.name ++ _Type.name;
};

[@genType]
[@dead "componentWithRenamedArgsProps"] [@react.component]
let componentWithRenamedArgs = (~_to, ~_Type, ~cb: cb) => {
  cb(~_to);
  React.string(_to.name ++ _Type.name);
};

[@genType]
[@dead "makeWithRefProps"] [@react.component]
let makeWithRef = (~vehicle, ref) => {
  switch (ref->Js.Nullable.toOption) {
  | Some(ref) =>
    <button ref={ReactDOMRe.Ref.domRef(ref)}>
      {React.string(vehicle.name)}
    </button>
  | None => React.null
  };
};

[@genType]
let testForwardRef = React.forwardRef(makeWithRef);

type r = {x: string};

[@genType]
[@dead "inputProps"] [@react.component]
let input =
  React.forwardRef((~r, (), ref) =>
    <div ref={Obj.magic(ref)}> {React.string(r.x)} </div>
  );

[@genType]
type callback('input, 'output) = React.callback('input, 'output);

[@genType]
type testReactContext = React.Context.t(int);

[@genType]
type testReactRef = React.Ref.t(int);

[@genType]
[@dead "polymorphicComponentProps"] [@react.component]
let polymorphicComponent = (~p as (x, _)) => React.string(x.name);

[@genType]
[@dead "functionReturningReactElementProps"] [@react.component]
let functionReturningReactElement = (~name) => React.string(name);

module RenderPropRequiresConversion = {
  [@genType]
[@dead "RenderPropRequiresConversion.makeProps"]   [@react.component]
  let make =
      (
        ~renderVehicle:
           {
             .
             "vehicle": vehicle,
             "number": int,
           } =>
           React.element,
      ) => {
    let car = {name: "Car"};
    renderVehicle({"vehicle": car, "number": 42});
  };
};

[@genType]
[@dead "aComponentWithChildrenProps"] [@react.component]
let aComponentWithChildren = (~vehicle, ~children) => {
  <div>
    {React.string("Another Hook " ++ vehicle.name)}
    <div> children </div>
  </div>;
};