// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

// Config is the configuration data passed to each generator.
type Config struct {
	// The base file name for files generated by this generator.
	OutputBase string

	// The directory to which C and C++ includes should be relative.
	IncludeBase string
}
