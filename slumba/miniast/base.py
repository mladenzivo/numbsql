"""
Constructing Python ASTs in Python is quite verbose, let's clean it up a bit.
"""

import ast
import copy


def comparisons(mapping):
    def decorator(cls):
        for method_name, op in mapping.items():
            setattr(
                cls,
                method_name,
                lambda self, other, op=op(): ast.Compare(
                    left=self,
                    ops=[op],
                    comparators=[other]
                )
            )
        return cls
    return decorator


@comparisons({
    '__eq__': ast.Eq,
    '__ne__': ast.NotEq,
    '__lt__': ast.Lt,
    '__le__': ast.LtE,
    '__gt__': ast.Gt,
    '__ge__': ast.GtE,
    'is_': ast.Is,
    'is_not': ast.IsNot,
})
class Comparable:
    pass


class Variable(ast.Name, Comparable):
    def __init__(self, id, ctx):
        super().__init__(id=id, ctx=ctx)

    def __getitem__(self, key):
        return sub(self, getidx(key))

    def assign(self, value):
        return ast.Assign(targets=[self], value=value)


class Load(Comparable):
    """
    API
    ---
    load.foo == ast.Name('foo', ctx=ast.Load())
    """
    __slots__ = ()

    def __getitem__(self, key):
        return Variable(id=key, ctx=ast.Load())

    __getattr__ = __getitem__


load = Load()


class Raise:
    __slots__ = ()

    def __call__(self, exception, cause=None):
        return ast.Raise(exc=exception, cause=cause)


raise_ = Raise()


class Store:
    __slots__ = ()

    def __getitem__(self, key):
        return Variable(id=key, ctx=ast.Store())

    __getattr__ = __getitem__


store = Store()


class Arg:
    __slots__ = ()

    def __getitem__(self, key):
        return ast.arg(arg=key, annotation=None)

    __getattr__ = __getitem__


arg = Arg()


def to_node(value):
    if isinstance(value, str):
        return ast.Str(s=value)
    elif isinstance(value, (int, float)):
        return ast.Num(n=value)
    return value


class Call:
    """
    API
    ---
    call.func(load.foo, nopython=TRUE)
    """
    __slots__ = ()

    def __getitem__(self, key):
        return lambda *args, **kwargs: self(load[key], *args, **kwargs)

    __getattr__ = __getitem__

    def __call__(self, callable, *args, **kwargs):
        return ast.Call(
            func=callable,
            args=list(map(to_node, args)),
            keywords=[
                ast.keyword(arg=key, value=value)
                for key, value in kwargs.items()
            ]
        )


call = Call()


class Attributable:
    __slots__ = 'parent',

    def __init__(self, parent):
        self.parent = parent

    def __getattr__(self, name):
        return ast.Attribute(value=self.parent, attr=name, ctx=ast.Load())


class Attr:
    __slots__ = ()

    def __getitem__(self, key):
        return Attributable(load[key])

    __getattr__ = __getitem__


attr = Attr()


class If:
    __slots__ = ()

    def __call__(self, test, body, orelse=None):
        if not isinstance(body, list):
            body = [body]

        if orelse is None:
            orelse = []

        if orelse is not None and not isinstance(orelse, list):
            orelse = [orelse]
        return ast.If(test=test, body=body, orelse=orelse)


if_ = If()


class IfElse:
    __slots__ = ()

    def __call__(self, test, body, orelse):
        return ast.IfExp(test, body, orelse)


def ifelse(test, body, orelse):
    return ast.IfExp(test, body, orelse)


class FunctionDeclaration:

    def __getitem__(self, name):
        return FunctionDef(name=name)


def_ = FunctionDeclaration()


class FunctionArguments:

    __slots__ = 'name', 'arguments'

    def __init__(self, name, arguments):
        self.name = name
        self.arguments = arguments

    def __call__(self, *body, returns=None):
        return ast.FunctionDef(
            name=self.name,
            args=self.arguments,
            body=list(body),
            decorator_list=[],
            returns=returns
        )


def decorate(*functions):
    def wrapper(function_definition):
        func_def = copy.copy(function_definition)
        func_def.decorator_list = list(functions)
        return func_def
    return wrapper


def mod(*body):
    return ast.Module(
        body=list(body)
    )


def expr(value):
    return ast.Expr(value=value)


class FunctionDef:
    __slots__ = 'name',

    def __init__(self, name):
        self.name = name

    def __call__(self, *ast_arguments):
        return FunctionArguments(
            self.name,
            ast.arguments(
                args=list(ast_arguments),
                vararg=None,
                kwonlyargs=[],
                kw_defaults=[],
                kwargs=None,
                defaults=[],
            )
        )


class Index:
    __slots__ = ()

    def __call__(self, index):
        return ast.Index(value=to_node(index), ctx=ast.Load())


getidx = Index()


class Subscript:
    __slots__ = ()

    def __call__(self, value, index):
        return ast.Subscript(
            value=value,
            slice=index,
            ctx=ast.Load(),
        )


sub = Subscript()


TRUE = ast.NameConstant(value=True)
FALSE = ast.NameConstant(value=False)
NONE = ast.NameConstant(value=None)


class Alias:
    """Shorter version of aliases used in `from foo import bar as baz`.

    API
    ---
    alias.foo == ast.alias(name=name, asname=None)
    """
    __slots__ = ()

    def __getattr__(self, name):
        return ast.alias(name=name, asname=None)

    def __getitem__(self, key):
        try:
            name, asname = key
        except ValueError:
            raise ValueError(
                'Only as imports are allowed with __getitem__, '
                'key length must be 2'
            )
        else:
            return ast.alias(name=name, asname=asname)


alias = Alias()


class ImportFrom:
    __slots__ = ()

    def __getattr__(self, name):
        return DottedModule(name)


import_from = ImportFrom()


class DottedModule:

    __slots__ = 'name',

    def __init__(self, name):
        self.name = name

    def __getitem__(self, key):
        names = [key] if isinstance(key, ast.alias) else list(key)
        return ast.ImportFrom(module=self.name, names=names, level=0)

    def __getattr__(self, name):
        return DottedModule('{}.{}'.format(self.name, name))
