// @target: es2015
// @strict: true
// @noEmit: true
const foo2 = foo1; // Error expected
const foo4 = foo3; // Error expected
const bar2 = bar1; // Error expected
export const Input = Type.Object({
    level1: Type.Object({
        level2: Type.Object({
            foo: Type.String(),
        })
    })
});
export const Output = Type.Object({
    level1: Type.Object({
        level2: Type.Object({
            foo: Type.String(),
            bar: Type.String(),
        })
    })
});
function problematicFunction1(ors) {
    return ors; // Error
}
function problematicFunction2(ors) {
    return ors; // Error
}
function problematicFunction3(ors) {
    return ors; // Error
}
