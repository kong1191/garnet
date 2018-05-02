// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const SourceFile = `
{{- define "GenerateSourceFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
#[macro_use]
extern crate fidl;
#[macro_use]
extern crate futures;
{{ range $crate := .ExternCrates -}}
extern crate {{ $crate }};
{{ end -}}

use fidl::encoding2::{Encodable, Decodable};
use futures::{Future, Stream, Never};

{{ range $const := .Consts -}}
{{ template "ConstDeclaration" $const }}
{{ end -}}
{{ range $enum := .Enums -}}
{{ template "EnumDeclaration" $enum }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionDeclaration" $union }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDeclaration" $struct }}
{{ end -}}
{{ range $interface := .Interfaces -}}
{{ template "InterfaceDeclaration" $interface }}
{{ end -}}
{{- end -}}
`
