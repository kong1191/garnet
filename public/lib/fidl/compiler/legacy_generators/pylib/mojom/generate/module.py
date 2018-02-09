# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This module's classes provide an interface to mojo modules. Modules are
# collections of interfaces and structs to be used by mojo ipc clients and
# servers.
#
# A simple interface would be created this way:
# module = mojom.generate.module.Module('Foo')
# interface = module.AddInterface('Bar')
# method = interface.AddMethod('Tat', 0)
# method.AddParameter('baz', 0, mojom.INT32)


class Kind(object):
  def __init__(self, spec=None):
    self.spec = spec
    self.parent_kind = None


class ReferenceKind(Kind):
  """ReferenceKind represents pointer types and handle types.
  A type is nullable if null (for pointer types) or invalid handle (for handle
  types) is a legal value for the type.
  """

  def __init__(self, spec=None, is_nullable=False):
    assert spec is None or is_nullable == spec.startswith('?')
    Kind.__init__(self, spec)
    self.is_nullable = is_nullable
    self.shared_definition = {}

  def MakeNullableKind(self):
    assert not self.is_nullable

    if self == STRING:
      return NULLABLE_STRING
    if self == HANDLE:
      return NULLABLE_HANDLE
    if self == DCPIPE:
      return NULLABLE_DCPIPE
    if self == DPPIPE:
      return NULLABLE_DPPIPE
    if self == CHANNEL:
      return NULLABLE_CHANNEL
    if self == VMO:
      return NULLABLE_VMO
    if self == PROCESS:
      return NULLABLE_PROCESS
    if self == THREAD:
      return NULLABLE_THREAD
    if self == EVENT:
      return NULLABLE_EVENT
    if self == PORT:
      return NULLABLE_PORT
    if self == JOB:
      return NULLABLE_JOB
    if self == SOCKET:
      return NULLABLE_SOCKET
    if self == EVENTPAIR:
      return NULLABLE_EVENTPAIR

    nullable_kind = type(self)()
    nullable_kind.shared_definition = self.shared_definition
    if self.spec is not None:
      nullable_kind.spec = '?' + self.spec
    nullable_kind.is_nullable = True

    return nullable_kind

  @classmethod
  def AddSharedProperty(cls, name):
    """Adds a property |name| to |cls|, which accesses the corresponding item in
       |shared_definition|.

       The reason of adding such indirection is to enable sharing definition
       between a reference kind and its nullable variation. For example:
         a = Struct('test_struct_1')
         b = a.MakeNullableKind()
         a.name = 'test_struct_2'
         print b.name  # Outputs 'test_struct_2'.
    """
    def Get(self):
      return self.shared_definition[name]

    def Set(self, value):
      self.shared_definition[name] = value

    setattr(cls, name, property(Get, Set))


# Initialize the set of primitive types. These can be accessed by clients.
BOOL                  = Kind('b')
INT8                  = Kind('i8')
INT16                 = Kind('i16')
INT32                 = Kind('i32')
INT64                 = Kind('i64')
UINT8                 = Kind('u8')
UINT16                = Kind('u16')
UINT32                = Kind('u32')
UINT64                = Kind('u64')
FLOAT                 = Kind('f')
DOUBLE                = Kind('d')
STRING                = ReferenceKind('s')
HANDLE                = ReferenceKind('h')
DCPIPE                = ReferenceKind('h:d:c')
DPPIPE                = ReferenceKind('h:d:p')
CHANNEL               = ReferenceKind('h:c')
VMO                   = ReferenceKind('h:v')
PROCESS               = ReferenceKind('h:p')
THREAD                = ReferenceKind('h:t')
EVENT                 = ReferenceKind('h:e')
PORT                  = ReferenceKind('h:r')
JOB                   = ReferenceKind('h:j')
SOCKET                = ReferenceKind('h:s')
EVENTPAIR             = ReferenceKind('h:ep')
NULLABLE_STRING       = ReferenceKind('?s', True)
NULLABLE_HANDLE       = ReferenceKind('?h', True)
NULLABLE_DCPIPE       = ReferenceKind('?h:d:c', True)
NULLABLE_DPPIPE       = ReferenceKind('?h:d:p', True)
NULLABLE_CHANNEL      = ReferenceKind('?h:c', True)
NULLABLE_VMO          = ReferenceKind('?h:v', True)
NULLABLE_PROCESS      = ReferenceKind('?h:p', True)
NULLABLE_THREAD       = ReferenceKind('?h:t', True)
NULLABLE_EVENT        = ReferenceKind('?h:e', True)
NULLABLE_PORT         = ReferenceKind('?h:r', True)
NULLABLE_JOB          = ReferenceKind('?h:j', True)
NULLABLE_SOCKET       = ReferenceKind('?h:s', True)
NULLABLE_EVENTPAIR    = ReferenceKind('?h:ep', True)

# Collection of all Primitive types
PRIMITIVES = (
  BOOL,
  INT8,
  INT16,
  INT32,
  INT64,
  UINT8,
  UINT16,
  UINT32,
  UINT64,
  FLOAT,
  DOUBLE,
  STRING,
  HANDLE,
  DCPIPE,
  DPPIPE,
  CHANNEL,
  VMO,
  PROCESS,
  THREAD,
  EVENT,
  PORT,
  JOB,
  SOCKET,
  EVENTPAIR,
  NULLABLE_STRING,
  NULLABLE_HANDLE,
  NULLABLE_DCPIPE,
  NULLABLE_DPPIPE,
  NULLABLE_CHANNEL,
  NULLABLE_VMO,
  NULLABLE_PROCESS,
  NULLABLE_THREAD,
  NULLABLE_EVENT,
  NULLABLE_PORT,
  NULLABLE_JOB,
  NULLABLE_SOCKET,
  NULLABLE_EVENTPAIR,
)

class NamedValue(object):
  def __init__(self, module=None, parent_kind=None, name=None):
    self.module = module
    if module:
      self.namespace = module.namespace
    self.parent_kind = parent_kind
    self.name = name
    self.imported_from = None

  def GetSpec(self):
    return (self.namespace + '.' +
        (self.parent_kind and (self.parent_kind.name + '.') or "") +
        self.name)


class BuiltinValue(object):
  def __init__(self, value):
    self.value = value


class ConstantValue(NamedValue):
  def __init__(self, module=None, parent_kind=None, constant=None):
    if constant:
      NamedValue.__init__(self, module, parent_kind, constant.name)
    else:
      NamedValue.__init__(self, module, parent_kind)
    self.constant = constant


class EnumValue(NamedValue):
  def __init__(self, module=None, enum=None, field=None):
    if enum and field:
      NamedValue.__init__(self, module, enum.parent_kind, field.name)
    else:
      NamedValue.__init__(self, module)
    self.enum = enum

  def GetSpec(self):
    return (self.namespace + '.' +
        (self.parent_kind and (self.parent_kind.name + '.') or "") +
        self.enum.name + '.' + self.name)


class Constant(object):
  def __init__(self, name=None, kind=None, value=None, parent_kind=None):
    self.name = name
    self.kind = kind
    self.value = value
    self.parent_kind = parent_kind


class Field(object):
  def __init__(self, name=None, kind=None, ordinal=None, default=None,
               attributes=None):
    if self.__class__.__name__ == 'Field':
      raise Exception()
    self.name = name
    self.kind = kind
    self.ordinal = ordinal
    self.default = default
    self.attributes = attributes


class StructField(Field): pass


class UnionField(Field): pass


class Struct(ReferenceKind):
  ReferenceKind.AddSharedProperty('name')
  ReferenceKind.AddSharedProperty('module')
  ReferenceKind.AddSharedProperty('imported_from')
  ReferenceKind.AddSharedProperty('fields')
  ReferenceKind.AddSharedProperty('attributes')
  ReferenceKind.AddSharedProperty('constants')
  ReferenceKind.AddSharedProperty('enums')
  ReferenceKind.AddSharedProperty('type_key')

  def __init__(self, name=None, module=None, attributes=None):
    if name is not None:
      spec = 'x:' + name
    else:
      spec = None
    ReferenceKind.__init__(self, spec)
    self.name = name
    self.module = module
    self.imported_from = None
    self.fields = []
    self.constants = []
    self.enums = []
    self.attributes = attributes

  def AddField(self, name, kind, ordinal=None, default=None, attributes=None):
    field = StructField(name, kind, ordinal, default, attributes)
    self.fields.append(field)
    return field


class Union(ReferenceKind):
  ReferenceKind.AddSharedProperty('name')
  ReferenceKind.AddSharedProperty('module')
  ReferenceKind.AddSharedProperty('imported_from')
  ReferenceKind.AddSharedProperty('fields')
  ReferenceKind.AddSharedProperty('attributes')
  ReferenceKind.AddSharedProperty('type_key')

  def __init__(self, name=None, module=None, attributes=None):
    if name is not None:
      spec = 'x:' + name
    else:
      spec = None
    ReferenceKind.__init__(self, spec)
    self.name = name
    self.module = module
    self.imported_from = None
    self.fields = []
    self.attributes = attributes

  def AddField(self, name, kind, ordinal=None, attributes=None):
    field = UnionField(name, kind, ordinal, None, attributes)
    self.fields.append(field)
    return field


class Array(ReferenceKind):
  ReferenceKind.AddSharedProperty('kind')
  ReferenceKind.AddSharedProperty('length')

  def __init__(self, kind=None, length=None):
    if kind is not None:
      if length is not None:
        spec = 'a%d:%s' % (length, kind.spec)
      else:
        spec = 'a:%s' % kind.spec

      ReferenceKind.__init__(self, spec)
    else:
      ReferenceKind.__init__(self)
    self.kind = kind
    self.length = length


class Map(ReferenceKind):
  ReferenceKind.AddSharedProperty('key_kind')
  ReferenceKind.AddSharedProperty('value_kind')

  def __init__(self, key_kind=None, value_kind=None):
    if (key_kind is not None and value_kind is not None):
      ReferenceKind.__init__(self,
                             'm[' + key_kind.spec + '][' + value_kind.spec +
                             ']')
      if IsNullableKind(key_kind):
        raise Exception("Nullable kinds cannot be keys in maps.")
      if IsStructKind(key_kind):
        # TODO(erg): It would sometimes be nice if we could key on struct
        # values. However, what happens if the struct has a handle in it? Or
        # non-copyable data like an array?
        raise Exception("Structs cannot be keys in maps.")
      if IsAnyHandleKind(key_kind):
        raise Exception("Handles cannot be keys in maps.")
      if IsInterfaceKind(key_kind):
        raise Exception("Interfaces cannot be keys in maps.")
      if IsArrayKind(key_kind):
        raise Exception("Arrays cannot be keys in maps.")
    else:
      ReferenceKind.__init__(self)

    self.key_kind = key_kind
    self.value_kind = value_kind


class InterfaceRequest(ReferenceKind):
  ReferenceKind.AddSharedProperty('kind')

  def __init__(self, kind=None):
    if kind is not None:
      if not isinstance(kind, Interface):
        raise Exception(
            "Interface request requires %r to be an interface." % kind.spec)
      ReferenceKind.__init__(self, 'r:' + kind.spec)
    else:
      ReferenceKind.__init__(self)
    self.kind = kind


class Parameter(object):
  def __init__(self, name=None, kind=None, ordinal=None, default=None,
               attributes=None):
    self.name = name
    self.ordinal = ordinal
    self.kind = kind
    self.default = default
    self.attributes = attributes


class Method(object):
  def __init__(self, interface, name, ordinal=None, attributes=None):
    self.interface = interface
    self.name = name
    self.ordinal = ordinal
    self.parameters = []
    self.response_parameters = None
    self.attributes = attributes

  def AddParameter(self, name, kind, ordinal=None, default=None,
                   attributes=None):
    parameter = Parameter(name, kind, ordinal, default, attributes)
    self.parameters.append(parameter)
    return parameter

  def AddResponseParameter(self, name, kind, ordinal=None, default=None,
                           attributes=None):
    if self.response_parameters == None:
      self.response_parameters = []
    parameter = Parameter(name, kind, ordinal, default, attributes)
    self.response_parameters.append(parameter)
    return parameter


class Interface(ReferenceKind):
  ReferenceKind.AddSharedProperty('module')
  ReferenceKind.AddSharedProperty('name')
  ReferenceKind.AddSharedProperty('imported_from')
  ReferenceKind.AddSharedProperty('methods')
  ReferenceKind.AddSharedProperty('attributes')
  ReferenceKind.AddSharedProperty('constants')
  ReferenceKind.AddSharedProperty('enums')
  ReferenceKind.AddSharedProperty('type_key')

  def __init__(self, name=None, module=None, attributes=None):
    if name is not None:
      spec = 'x:' + name
    else:
      spec = None
    ReferenceKind.__init__(self, spec)
    self.module = module
    self.name = name
    self.imported_from = None
    self.methods = []
    self.enums = []
    self.constants = []
    self.attributes = attributes

  def AddMethod(self, name, ordinal=None, attributes=None):
    method = Method(self, name, ordinal, attributes)
    self.methods.append(method)
    return method

  # TODO(451323): Remove when the language backends no longer rely on this.
  @property
  def client(self):
    return None


class EnumField(object):
  def __init__(self, name=None, value=None, attributes=None,
               numeric_value=None):
    self.name = name
    self.value = value
    self.attributes = attributes
    self.numeric_value = numeric_value


class Enum(Kind):
  def __init__(self, name=None, module=None, attributes=None):
    self.module = module
    self.name = name
    self.imported_from = None
    if name is not None:
      spec = 'x:' + name
    else:
      spec = None
    Kind.__init__(self, spec)
    self.fields = []
    self.attributes = attributes


class Module(object):
  def __init__(self, name=None, namespace=None, attributes=None):
    self.name = name
    self.path = name
    self.namespace = namespace
    self.structs = []
    self.unions = []
    self.interfaces = []
    self.enums = []
    self.constants = []
    self.kinds = {}
    self.attributes = attributes
    self.imports = []
    # Transitive imports should contain all of the module's imports plus
    # all of their imports recursively.
    self.transitive_imports = []

  def AddInterface(self, name, attributes=None):
    interface = Interface(name, self, attributes)
    self.interfaces.append(interface)
    return interface

  def AddStruct(self, name, attributes=None):
    struct = Struct(name, self, attributes)
    self.structs.append(struct)
    return struct

  def AddUnion(self, name, attributes=None):
    union = Union(name, self, attributes)
    self.unions.append(union)
    return union

def GetPackageName(kind):
  """Get the package name from the given kind's module."""
  return kind.module.name.split('.')[0]

def IsBoolKind(kind):
  return kind.spec == BOOL.spec


def IsFloatKind(kind):
  return kind.spec == FLOAT.spec


def IsDoubleKind(kind):
  return kind.spec == DOUBLE.spec


def IsIntegralKind(kind):
  return (kind.spec == BOOL.spec or
          kind.spec == INT8.spec or
          kind.spec == INT16.spec or
          kind.spec == INT32.spec or
          kind.spec == INT64.spec or
          kind.spec == UINT8.spec or
          kind.spec == UINT16.spec or
          kind.spec == UINT32.spec or
          kind.spec == UINT64.spec)

def IsNumericalKind(kind):
  return (IsBoolKind(kind) or
          IsFloatKind(kind) or
          IsDoubleKind(kind) or
          IsIntegralKind(kind))

def IsStringKind(kind):
  return kind.spec == STRING.spec or kind.spec == NULLABLE_STRING.spec


def IsGenericHandleKind(kind):
  return kind.spec == HANDLE.spec or kind.spec == NULLABLE_HANDLE.spec


def IsChannelKind(kind):
  return kind.spec == CHANNEL.spec or kind.spec == NULLABLE_CHANNEL.spec

def IsVMOKind(kind):
  return (kind.spec == VMO.spec or kind.spec == NULLABLE_VMO.spec)

def IsProcessKind(kind):
  return (kind.spec == PROCESS.spec or kind.spec == NULLABLE_PROCESS.spec)

def IsThreadKind(kind):
  return (kind.spec == THREAD.spec or kind.spec == NULLABLE_THREAD.spec)

def IsEventKind(kind):
  return (kind.spec == EVENT.spec or kind.spec == NULLABLE_EVENT.spec)

def IsPortKind(kind):
  return (kind.spec == PORT.spec or kind.spec == NULLABLE_PORT.spec)

def IsJobKind(kind):
  return (kind.spec == JOB.spec or kind.spec == NULLABLE_JOB.spec)

def IsSocketKind(kind):
  return (kind.spec == SOCKET.spec or kind.spec == NULLABLE_SOCKET.spec)

def IsEventPairKind(kind):
  return (kind.spec == EVENTPAIR.spec or kind.spec == NULLABLE_EVENTPAIR.spec)

def IsStructKind(kind):
  return isinstance(kind, Struct)


def IsUnionKind(kind):
  return isinstance(kind, Union)


def IsArrayKind(kind):
  return isinstance(kind, Array)


def IsInterfaceKind(kind):
  return isinstance(kind, Interface)


def IsInterfaceRequestKind(kind):
  return isinstance(kind, InterfaceRequest)


def IsEnumKind(kind):
  return isinstance(kind, Enum)


def IsReferenceKind(kind):
  return isinstance(kind, ReferenceKind)


def IsNullableKind(kind):
  return IsReferenceKind(kind) and kind.is_nullable


def IsMapKind(kind):
  return isinstance(kind, Map)


def IsObjectKind(kind):
  return IsPointerKind(kind) or IsUnionKind(kind)


def IsPointerKind(kind):
  return (IsStructKind(kind) or IsArrayKind(kind) or IsStringKind(kind) or
          IsMapKind(kind))


# Please note that interface is not considered as handle kind, since it is an
# aggregate type consisting of a handle and a version number.
def IsAnyHandleKind(kind):
  return (IsGenericHandleKind(kind) or
          IsChannelKind(kind) or
          IsVMOKind(kind) or
          IsProcessKind(kind) or
          IsThreadKind(kind) or
          IsEventKind(kind) or
          IsPortKind(kind) or
          IsJobKind(kind) or
          IsSocketKind(kind) or
          IsEventPairKind(kind) or
          IsInterfaceRequestKind(kind))

# Please note that interface is not considered as handle kind, since it is an
# aggregate type consisting of a handle and a version number.
# TODO: Socket is also cloneable but currently causes a compilation issue.
def IsAnyUncloneableHandleKind(kind):
  return (IsChannelKind(kind) or
          IsSocketKind(kind) or
          IsInterfaceRequestKind(kind))

def IsMoveOnlyKind(kind):
  return (not IsStringKind(kind) and IsObjectKind(kind)) or \
      IsAnyHandleKind(kind) or IsInterfaceKind(kind)

def ContainsUncloneableHandles(kind, visited_kinds):
  if kind in visited_kinds:
    # No need to examine the kind again.
    return False
  visited_kinds.add(kind)
  if IsAnyUncloneableHandleKind(kind) or IsInterfaceKind(kind):
    return True
  if IsArrayKind(kind):
    return ContainsUncloneableHandles(kind.kind, visited_kinds)
  if IsStructKind(kind) or IsUnionKind(kind):
    for field in kind.fields:
      if ContainsUncloneableHandles(field.kind, visited_kinds):
        return True
  if IsMapKind(kind):
    # No need to examine the key kind, only primitive kinds and non-nullable
    # string are allowed to be key kinds.
    return ContainsUncloneableHandles(kind.value_kind, visited_kinds)
  return False


def IsCloneableKind(kind):
  return not ContainsUncloneableHandles(kind, set())


def HasCallbacks(interface):
  for method in interface.methods:
    if method.response_parameters != None:
      return True
  return False
