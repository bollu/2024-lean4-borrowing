/*
Copyright (c) 2015 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "library/compiler/util.h"
#include "library/compiler/lcnf.h"
#include "library/compiler/elim_dead_let.h"
#include "library/compiler/cse.h"
#include "library/compiler/specialize.h"
#include "library/compiler/llnf.h"
#include "library/compiler/compiler.h"
#include "library/compiler/export_attribute.h"
#include "library/compiler/extern_attribute.h"
#include "library/compiler/implemented_by_attribute.h"
#include "library/compiler/borrowed_annotation.h"
#include "library/compiler/ll_infer_type.h"
#include "library/compiler/init_attribute.h"

namespace lean {
void initialize_compiler_module() {
    initialize_implemented_by_attribute();
    initialize_init_attribute();
    initialize_compiler_util();
    initialize_lcnf();
    initialize_elim_dead_let();
    initialize_cse();
    initialize_specialize();
    initialize_llnf();
    initialize_compiler();
    initialize_export_attribute();
    initialize_extern_attribute();
    initialize_borrowed_annotation();
    initialize_ll_infer_type();
}

void finalize_compiler_module() {
    finalize_ll_infer_type();
    finalize_borrowed_annotation();
    finalize_extern_attribute();
    finalize_export_attribute();
    finalize_compiler();
    finalize_llnf();
    finalize_specialize();
    finalize_cse();
    finalize_elim_dead_let();
    finalize_lcnf();
    finalize_compiler_util();
    finalize_init_attribute();
    finalize_implemented_by_attribute();
}
}
