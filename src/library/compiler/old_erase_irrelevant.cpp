/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "runtime/sstream.h"
#include "kernel/instantiate.h"
#include "library/util.h"
#include "library/string.h"
#include "library/constants.h"
#include "library/aux_recursors.h"
#include "library/compiler/old_util.h"
#include "library/compiler/nat_value.h"
#include "library/compiler/comp_irrelevant.h"
#include "library/compiler/compiler_step_visitor.h"

namespace lean {
class old_erase_irrelevant_fn : public compiler_step_visitor {
    virtual expr visit_sort(expr const &) override {
        return mk_enf_neutral();
    }

    virtual expr visit_pi(expr const &) override {
        return mk_enf_neutral();
    }

    bool is_comp_irrelevant(expr const & e) {
        try {
            /* TODO(Leo): revise this code, infer/whnf are problematic here since
               types may already have been erased in 'e'. */
            expr type = ctx().whnf(ctx().infer(e));
            return is_sort(type) || ctx().is_prop(type);
        } catch (exception &) {
            return false;
        }
    }

    virtual expr visit_mdata(expr const & e) override {
        if (is_marked_as_comp_irrelevant(e)) {
            return mk_enf_neutral();
        } else if (is_comp_irrelevant(e)) {
            return mk_enf_neutral();
        } else {
            return visit(mdata_expr(e));
        }
    }

    virtual expr visit_local(expr const & e) override {
        if (is_comp_irrelevant(e))
            return mk_enf_neutral();
        else
            return e;
    }

    virtual expr visit_constant(expr const & e) override {
        if (is_comp_irrelevant(e)) {
            return mk_enf_neutral();
        } else if (const_name(e) == get_lc_unreachable_name()) {
            return mk_enf_unreachable();
        } else {
            /* Erase universe level information */
            return mk_constant(const_name(e));
        }
    }

    expr erase_type(expr const & e) {
        if (!has_loose_bvars(e) && !has_local(e))
            return e; // keep closed types for runtime debugger
        else
            return mk_enf_neutral();
    }

    static bool is_irrelevant_lambda_let_body(expr e) {
        while (true) {
            if (is_lambda(e))
                e = binding_body(e);
            else if (is_let(e))
                e = let_body(e);
            else
                return is_enf_neutral(e);
        }
    }

    expr erase_lambda_let_types_when_relevant(expr const & e) {
        if (is_lambda(e)) {
            return mk_lambda(binding_name(e), erase_type(binding_domain(e)),
                             erase_lambda_let_types_when_relevant(binding_body(e)));
        } else if (is_let(e)) {
            return mk_let(let_name(e), erase_type(let_type(e)), let_value(e), erase_lambda_let_types_when_relevant(let_body(e)));
        } else {
            return e;
        }
    }

    expr erase_lambda_let_types_when_irrelevant(expr const & e) {
        if (is_lambda(e)) {
            return mk_lambda(binding_name(e), mk_enf_neutral(),
                             erase_lambda_let_types_when_irrelevant(binding_body(e)));
        } else if (is_let(e)) {
            return erase_lambda_let_types_when_irrelevant(let_body(e));
        } else {
            return e;
        }
    }

    expr erase_lambda_let_types(expr const & e) {
        if (is_irrelevant_lambda_let_body(e))
            return erase_lambda_let_types_when_irrelevant(e);
        else
            return erase_lambda_let_types_when_relevant(e);
    }

    virtual expr visit_lambda(expr const & e) override {
        return erase_lambda_let_types(compiler_step_visitor::visit_lambda(e));
    }

    virtual expr visit_let(expr const & e) override {
        return erase_lambda_let_types(compiler_step_visitor::visit_let(e));
    }

    /* Process minor premises and args, and distribute args over minors.
       The size of cnames is nminors, and it contains the names of the constructors. */
    void visit_minors(unsigned nparams, unsigned nminors, expr * minors, name const * cnames,
                      unsigned nargs, expr * args) {
        if (nargs > 0) {
            for (unsigned i = 0; i < nargs; i++)
                args[i] = visit(args[i]);
            /* distribute args over minors */
            for (unsigned i = 0; i < nminors; i++) {
                unsigned carity = get_constructor_arity(env(), cnames[i]);
                lean_assert(carity >= nparams);
                unsigned data_sz = carity - nparams;
                type_context_old::tmp_locals locals(ctx());
                expr new_minor   = minors[i];
                for (unsigned j = 0; j < data_sz; j++) {
                    if (!is_lambda(new_minor))
                        throw exception("unexpected occurrence of 'cases_on' expression, "
                                        "the minor premise is expected to be a lambda-expression");
                    expr local = locals.push_local_from_binding(new_minor);
                    new_minor  = instantiate(binding_body(new_minor), local);
                }
                new_minor = visit(new_minor);
                new_minor = beta_reduce(mk_app(new_minor, nargs, args));
                minors[i] = erase_lambda_let_types(locals.mk_lambda(new_minor));
            }
        } else {
            for (unsigned i = 0; i < nminors; i++)
                minors[i] = visit(minors[i]);
        }
    }

    /* We keep only the major and minor premises in cases_on applications. */
    expr visit_cases_on(expr const & fn, buffer<expr> & args) {
        name const & rec_name       = const_name(fn);
        name const & I_name         = rec_name.get_prefix();
        if (I_name == get_false_name())
            return mk_enf_unreachable();
        constant_info I_info        = env().get(I_name);
        inductive_val I_val         = I_info.to_inductive_val();
        unsigned nparams            = I_val.get_nparams();
        unsigned nminors            = length(I_val.get_cnstrs());
        unsigned nindices           = I_val.get_nindices();
        unsigned arity              = nparams + 1 /* typeformer/motive */ + nindices + 1 /* major premise */ + nminors;
        lean_assert(args.size() >= arity);
        /* TODO(Leo, Daniel): we need a way to check whether the user is trying to cases_on
           on an auxiliary datatype
        */
        buffer<name> cnames;
        get_constructor_names(env(), I_name, cnames);
        expr * minors        = args.data() + nparams + 1 + nindices + 1;
        unsigned nextra_args = args.size() - arity;
        expr * extra_args    = args.data() + arity;
        visit_minors(nparams, nminors, minors, cnames.data(), nextra_args, extra_args);
        expr major  = visit(args[nparams + 1 + nindices]);
        expr new_fn = visit(fn);
        return mk_app(mk_app(new_fn, major), nminors, minors);
    }

    /* We keep only the major and minor premises in rec applications.
       This method also converts the rec into cases_on */
    expr visit_rec(expr const & fn, buffer<expr> & args) {
        name const & rec_name       = const_name(fn);
        name const & I_name         = rec_name.get_prefix();
        if (I_name == get_false_name())
            return mk_enf_unreachable();

        /* This preprocessing step assumes that recursive recursors have already been eliminated */
        lean_assert(!is_recursive_datatype(env(), I_name));
        constant_info rec_info      = env().get(rec_name);
        recursor_val rec_val        = rec_info.to_recursor_val();
        unsigned nparams            = rec_val.get_nparams();
        unsigned nminors            = rec_val.get_nminors();
        unsigned nindices           = rec_val.get_nindices();
        unsigned nmotives           = rec_val.get_nmotives();
        unsigned arity              = nparams + nmotives + nminors + nindices + 1 /* major premise */;
        lean_assert(args.size() >= arity);
        buffer<name> cnames;
        get_constructor_names(env(), I_name, cnames);
        expr new_fn = mk_constant(name(I_name, "cases_on"));
        expr major  = visit(args[nparams + 1 + nminors + nindices]);
        expr * minors        = args.data() + nparams + 1;
        unsigned nextra_args = args.size() - arity;
        expr * extra_args    = args.data() + arity;
        visit_minors(nparams, nminors, minors, cnames.data(), nextra_args, extra_args);
        return mk_app(mk_app(new_fn, major), nminors, minors);
    }

    expr add_args(expr e, unsigned start_idx, buffer<expr> const & args) {
        for (unsigned i = start_idx; i < args.size(); i++)
            e = mk_app(e, visit(args[i]));
        return beta_reduce(e);
    }

    /* Remove eq.rec applications since they are just "type-casting" operations. */
    expr visit_eq_rec(buffer<expr> & args) {
        lean_assert(args.size() >= 6);
        expr major = visit(args[3]);
        return add_args(major, 6, args);
    }

    expr consume_lambdas(type_context_old::tmp_locals & locals, expr e) {
        while (true) {
            if (is_lambda(e)) {
                expr local = locals.push_local_from_binding(e);
                e = instantiate(binding_body(e), local);
            } else {
                return beta_reduce(e);
            }
        }
    }

    /* We can eliminate no_confusion applications, they do not add any relevant information to the environment */
    expr visit_no_confusion(expr const & fn, buffer<expr> const & args) {
        lean_assert(is_constant(fn));
        name const & no_confusion_name  = const_name(fn);
        name const & I_name             = no_confusion_name.get_prefix();
        constant_info I_info            = env().get(I_name);
        inductive_val I_val             = I_info.to_inductive_val();
        unsigned nparams                = I_val.get_nparams();
        unsigned nindices               = I_val.get_nindices();
        DEBUG_CODE(unsigned basic_arity = nparams + nindices + 1 /* motive */ + 2 /* lhs/rhs */ + 1 /* equality */;);
        lean_assert(args.size() >= basic_arity);
        expr lhs                        = ctx().whnf(args[nparams + nindices + 1]);
        expr rhs                        = ctx().whnf(args[nparams + nindices + 2]);
        optional<name> lhs_constructor  = is_constructor_app(env(), lhs);
        optional<name> rhs_constructor  = is_constructor_app(env(), rhs);
        if (!lhs_constructor || !rhs_constructor)
            throw exception(sstream() << "code generation failed, unsupported occurrence of '" << no_confusion_name << "', constructors expected");
        if (lhs_constructor != rhs_constructor)
            return mk_enf_unreachable();
        lean_assert(args.size() >= basic_arity + 1);
        expr major = args[nparams + nindices + 4];
        type_context_old::tmp_locals locals(ctx());
        major = consume_lambdas(locals, major);
        major = visit(major);
        major = erase_lambda_let_types(locals.mk_lambda(major));
        expr r = major;

        unsigned c_data_sz = get_constructor_arity(env(), *lhs_constructor) - nparams;
        for (unsigned i = 0; i < c_data_sz; i++)
            r = mk_app(r, mk_enf_neutral()); // add dummy proofs
        /* add remaining arguments */
        return add_args(r, nparams + nindices + 5, args);
    }

    /* Treat subtype.mk as the identity function */
    expr visit_subtype_mk(buffer<expr> const & args) {
        lean_assert(args.size() >= 4);
        expr r = visit(args[2]);
        return add_args(r, 4, args);
    }

    /* Eliminate subtype.rec */
    expr visit_subtype_rec(buffer<expr> const & args) {
        lean_assert(args.size() >= 5);
        expr minor = visit(args[3]);
        expr major = visit(args[4]);
        expr r     = mk_app(minor, major, mk_enf_neutral());
        return add_args(r, 5, args);
    }

    /* subtype.val is also compiled as the identity function */
    expr visit_subtype_val(buffer<expr> const & args) {
        lean_assert(args.size() >= 3);
        expr r = visit(args[2]);
        return add_args(r, 3, args);
    }

    expr visit_acc_cases_on(buffer<expr> & args) {
        lean_assert(args.size() >= 6);
        expr a     = visit(args[3]);
        expr minor = visit(args[5]);
        /* acc.cases_on has type
           Π {A : Type} {R : A → A → Prop} {C : A → Type} {a : A},
             acc R a → (Π (x : A), (∀ (y : A), R y x → acc R y) → C x) → C a

           We replace an acc.cases_on application with the minor premise
           applied to {a : A} and an comp irrelevant term.
        */
        expr r = beta_reduce(mk_app(minor, a, mk_enf_neutral()));
        return add_args(r, 6, args);
    }

    expr visit_and_cases_on(buffer<expr> & args) {
        lean_assert(args.size() >= 5);
        expr minor = visit(args[4]);
        /* and.cases_on has type
          and.cases_on : Π {a b : Prop} {C : Sort u_1}, a ∧ b → (a → b → C) → C

           We replace an and.cases_on application with the minor premise
           applied to neutral elements. */
        expr r = beta_reduce(mk_app(minor, mk_enf_neutral(), mk_enf_neutral()));
        return add_args(r, 5, args);
    }

    /* See and.cases_on */
    expr visit_and_rec(buffer<expr> & args) {
        lean_assert(args.size() >= 5);
        expr minor = visit(args[3]);
        expr r = beta_reduce(mk_app(minor, mk_enf_neutral(), mk_enf_neutral()));
        return add_args(r, 5, args);
    }

    expr visit_quot_lift(buffer<expr> const & args) {
        lean_assert(args.size() >= 6);
        expr f = visit(args[3]);
        expr q = visit(args[5]);
        expr r = beta_reduce(mk_app(f, q));
        return add_args(r, 6, args);
    }

    expr visit_quot_mk(buffer<expr> const & args) {
        lean_assert(args.size() >= 3);
        expr r = visit(args[2]);
        return add_args(r, 3, args);
    }

    virtual expr visit_app(expr const & e) override {
        if (is_comp_irrelevant(e))
            return mk_enf_neutral();
        if (auto n = to_nat_value(ctx(), e))
            return *n;
        buffer<expr> args;
        expr const & fn = get_app_args(e, args);
        if (is_lambda(fn)) {
            return visit(beta_reduce(e));
        } else if (is_constant(fn)) {
            name const & n = const_name(fn);
            if (n == get_eq_rec_name()) {
                return visit_eq_rec(args);
            } else if (n == get_acc_cases_on_name()) {
                return visit_acc_cases_on(args);
            } else if (n == get_and_cases_on_name()) {
                return visit_and_cases_on(args);
            } else if (n == get_and_rec_name()) {
                return visit_and_rec(args);
            } else if (n == get_quot_lift_name()) {
                return visit_quot_lift(args);
            } else if (n == get_quot_mk_name()) {
                return visit_quot_mk(args);
            } else if (n == get_subtype_rec_name()) {
                return visit_subtype_rec(args);
            } else if (is_cases_on_recursor(env(), n)) {
                return visit_cases_on(fn, args);
            } else if (is_recursor(env(), n)) {
                return visit_rec(fn, args);
            } else if (is_no_confusion(env(), n)) {
                return visit_no_confusion(fn, args);
            } else if (n == get_subtype_mk_name()) {
                return visit_subtype_mk(args);
            } else if (n == get_subtype_val_name()) {
                return visit_subtype_val(args);
            } else if (n == get_lc_unreachable_name()) {
                return mk_enf_unreachable();
            }
        }
        return compiler_step_visitor::visit_app(e);
    }

public:
    old_erase_irrelevant_fn(environment const & env, abstract_context_cache & cache):
        compiler_step_visitor(env, cache) {}
};

expr old_erase_irrelevant(environment const & env, abstract_context_cache & cache, expr const & e) {
    return old_erase_irrelevant_fn(env, cache)(e);
}
}
