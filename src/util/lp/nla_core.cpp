/*++
Copyright (c) 2017 Microsoft Corporation

Module Name:

    <name>

Abstract:

    <abstract>

Author:
    Nikolaj Bjorner (nbjorner)
    Lev Nachmanson (levnach)

Revision History:


--*/
#include "util/lp/nla_core.h"
#include "util/lp/factorization_factory_imp.h"
namespace nla {

core::core(lp::lar_solver& s) :
    m_evars(),
    m_lar_solver(s),
    m_tangents(this),
    m_basics(this) {
}
    
bool core::compare_holds(const rational& ls, llc cmp, const rational& rs) const {
    switch(cmp) {
    case llc::LE: return ls <= rs;
    case llc::LT: return ls < rs;
    case llc::GE: return ls >= rs;
    case llc::GT: return ls > rs;
    case llc::EQ: return ls == rs;
    case llc::NE: return ls != rs;
    default: SASSERT(false);
    };
        
    return false;
}

rational core::value(const lp::lar_term& r) const {
    rational ret(0);
    for (const auto & t : r.coeffs()) {
        ret += t.second * vvr(t.first);
    }
    return ret;
}

lp::lar_term core::subs_terms_to_columns(const lp::lar_term& t) const {
    lp::lar_term r;
    for (const auto& p : t) {
        lpvar j = p.var();
        if (m_lar_solver.is_term(j))
            j = m_lar_solver.map_term_index_to_column_index(j);
        r.add_coeff_var(p.coeff(), j);
    }
    return r;
} 
    
bool core::ineq_holds(const ineq& n) const {
    lp::lar_term t = subs_terms_to_columns(n.term());
    return compare_holds(value(t), n.cmp(), n.rs());
}

bool core::lemma_holds(const lemma& l) const {
    for(const ineq &i : l.ineqs()) {
        if (ineq_holds(i))
            return true;
    }
    return false;
}
    
svector<lpvar> core::sorted_vars(const factor& f) const {
    if (f.is_var()) {
        svector<lpvar> r; r.push_back(f.index());
        return r;
    }
    TRACE("nla_solver", tout << "nv";);
    return m_rm_table.rms()[f.index()].vars();
}

// the value of the factor is equal to the value of the variable multiplied
// by the canonize_sign
rational core::canonize_sign(const factor& f) const {
    return f.is_var()?
        canonize_sign_of_var(f.index()) : m_rm_table.rms()[f.index()].orig_sign();
}

rational core::canonize_sign_of_var(lpvar j) const {
    return m_evars.find(j).rsign();        
}
    
// the value of the rooted monomias is equal to the value of the variable multiplied
// by the canonize_sign
rational core::canonize_sign(const rooted_mon& m) const {
    return m.orig().sign();
}
    
// returns the monomial index
unsigned core::add(lpvar v, unsigned sz, lpvar const* vs) {
    m_monomials.push_back(monomial(v, sz, vs));
    TRACE("nla_solver", print_monomial(m_monomials.back(), tout););
    const auto & m = m_monomials.back();
    SASSERT(m_mkeys.find(m.vars()) == m_mkeys.end());
    return m_mkeys[m.vars()] = m_monomials.size() - 1;
}
    
void core::push() {
    TRACE("nla_solver",);
    m_monomials_counts.push_back(m_monomials.size());
    m_evars.push();
}

void core::deregister_monomial_from_rooted_monomials (const monomial & m, unsigned i){
    rational sign = rational(1);
    svector<lpvar> vars = reduce_monomial_to_rooted(m.vars(), sign);
    NOT_IMPLEMENTED_YET();
}

void core::deregister_monomial_from_tables(const monomial & m, unsigned i){
    TRACE("nla_solver", tout << "m = "; print_monomial(m, tout););
    m_mkeys.erase(m.vars());
    deregister_monomial_from_rooted_monomials(m, i);     
}
     
void core::pop(unsigned n) {
    TRACE("nla_solver", tout << "n = " << n <<
          " , m_monomials_counts.size() = " << m_monomials_counts.size() << ", m_monomials.size() = " << m_monomials.size() << "\n"; );
    if (n == 0) return;
    unsigned new_size = m_monomials_counts[m_monomials_counts.size() - n];
    TRACE("nla_solver", tout << "new_size = " << new_size << "\n"; );
        
    for (unsigned i = m_monomials.size(); i-- > new_size; ){
        deregister_monomial_from_tables(m_monomials[i], i);
    }
    m_monomials.shrink(new_size);
    m_monomials_counts.shrink(m_monomials_counts.size() - n);
    m_evars.pop(n);
}

rational core::mon_value_by_vars(unsigned i) const {
    return product_value(m_monomials[i]);
}
template <typename T>
rational core::product_value(const T & m) const {
    rational r(1);
    for (auto j : m) {
        r *= m_lar_solver.get_column_value_rational(j);
    }
    return r;
}
    
// return true iff the monomial value is equal to the product of the values of the factors
bool core::check_monomial(const monomial& m) const {
    SASSERT(m_lar_solver.get_column_value(m.var()).is_int());        
    return product_value(m) == m_lar_solver.get_column_value_rational(m.var());
}
    
void core::explain(const monomial& m, lp::explanation& exp) const {       
    for (lpvar j : m)
        explain(j, exp);
}

void core::explain(const rooted_mon& rm, lp::explanation& exp) const {
    auto & m = m_monomials[rm.orig_index()];
    explain(m, exp);
}

void core::explain(const factor& f, lp::explanation& exp) const {
    if (f.type() == factor_type::VAR) {
        explain(f.index(), exp);
    } else {
        explain(m_monomials[m_rm_table.rms()[f.index()].orig_index()], exp);
    }
}

void core::explain(lpvar j, lp::explanation& exp) const {
    m_evars.explain(j, exp);
}

template <typename T>
std::ostream& core::print_product(const T & m, std::ostream& out) const {
    for (unsigned k = 0; k < m.size(); k++) {
        out << "(" << m_lar_solver.get_variable_name(m[k]) << "=" << vvr(m[k]) << ")";
        if (k + 1 < m.size()) out << "*";
    }
    return out;
}

std::ostream & core::print_factor(const factor& f, std::ostream& out) const {
    if (f.is_var()) {
        out << "VAR,  ";
        print_var(f.index(), out);
    } else {
        out << "PROD, ";
        print_product(m_rm_table.rms()[f.index()].vars(), out);
    }
    out << "\n";
    return out;
}

std::ostream & core::print_factor_with_vars(const factor& f, std::ostream& out) const {
    if (f.is_var()) {
        print_var(f.index(), out);
    } else {
        out << " RM = "; print_rooted_monomial_with_vars(m_rm_table.rms()[f.index()], out);
        out << "\n orig mon = "; print_monomial_with_vars(m_monomials[m_rm_table.rms()[f.index()].orig_index()], out);
    }
    return out;
}

std::ostream& core::print_monomial(const monomial& m, std::ostream& out) const {
    out << "( [" << m.var() << "] = " << m_lar_solver.get_variable_name(m.var()) << " = " << vvr(m.var()) << " = ";
    print_product(m.vars(), out);
    out << ")\n";
    return out;
}

std::ostream& core::print_point(const point &a, std::ostream& out) const {
    out << "(" << a.x <<  ", " << a.y << ")";
    return out;
}
    
std::ostream& core::print_tangent_domain(const point &a, const point &b, std::ostream& out) const {
    out << "("; print_point(a, out);  out <<  ", "; print_point(b, out); out <<  ")";
    return out;
}

std::ostream& core::print_bfc(const bfc& m, std::ostream& out) const {
    out << "( x = "; print_factor(m.m_x, out); out <<  ", y = "; print_factor(m.m_y, out); out <<  ")";
    return out;
}

std::ostream& core::print_monomial(unsigned i, std::ostream& out) const {
    return print_monomial(m_monomials[i], out);
}

std::ostream& core::print_monomial_with_vars(unsigned i, std::ostream& out) const {
    return print_monomial_with_vars(m_monomials[i], out);
}

template <typename T>
std::ostream& core::print_product_with_vars(const T& m, std::ostream& out) const {
    print_product(m, out);
    out << '\n';
    for (unsigned k = 0; k < m.size(); k++) {
        print_var(m[k], out);
    }
    return out;
}

std::ostream& core::print_monomial_with_vars(const monomial& m, std::ostream& out) const {
    out << "["; print_var(m.var(), out) << "]\n";
    for(lpvar j: m)
        print_var(j, out);
    out << ")\n";
    return out;
}

std::ostream& core::print_rooted_monomial(const rooted_mon& rm, std::ostream& out) const {
    out << "vars = "; 
    print_product(rm.vars(), out);
    out << "\n orig = "; print_monomial(m_monomials[rm.orig_index()], out);
    out << ", orig sign = " << rm.orig_sign() << "\n";
    return out;
}

std::ostream& core::print_rooted_monomial_with_vars(const rooted_mon& rm, std::ostream& out) const {
    out << "vars = "; 
    print_product(rm.vars(), out);
    out << "\n orig = "; print_monomial_with_vars(m_monomials[rm.orig_index()], out);
    out << ", orig sign = " << rm.orig_sign() << "\n";
    out << ", vvr(rm) = " << vvr(rm) << "\n";
        
    return out;
}

std::ostream& core::print_explanation(const lp::explanation& exp, std::ostream& out) const {
    out << "expl: ";
    for (auto &p : exp) {
        out << "(" << p.second << ")";
        m_lar_solver.print_constraint(p.second, out);
        out << "      ";
    }
    out << "\n";
    return out;
}

bool core::explain_upper_bound(const lp::lar_term& t, const rational& rs, lp::explanation& e) const {
    rational b(0); // the bound
    for (const auto& p : t) {
        rational pb;
        if (explain_coeff_upper_bound(p, pb, e)) {
            b += pb;
        } else {
            e.clear();
            return false;
        }
    }
    if (b > rs ) {
        e.clear();
        return false;
    }
    return true;
}
bool core::explain_lower_bound(const lp::lar_term& t, const rational& rs, lp::explanation& e) const {
    rational b(0); // the bound
    for (const auto& p : t) {
        rational pb;
        if (explain_coeff_lower_bound(p, pb, e)) {
            b += pb;
        } else {
            e.clear();
            return false;
        }
    }
    if (b < rs ) {
        e.clear();
        return false;
    }
    return true;
}

bool core:: explain_coeff_lower_bound(const lp::lar_term::ival& p, rational& bound, lp::explanation& e) const {
    const rational& a = p.coeff();
    SASSERT(!a.is_zero());
    unsigned c; // the index for the lower or the upper bound
    if (a.is_pos()) {
        unsigned c = m_lar_solver.get_column_lower_bound_witness(p.var());
        if (c + 1 == 0)
            return false;
        bound = a * m_lar_solver.get_lower_bound(p.var()).x;
        e.add(c);
        return true;
    }
    // a.is_neg()
    c = m_lar_solver.get_column_upper_bound_witness(p.var());
    if (c + 1 == 0)
        return false;
    bound = a * m_lar_solver.get_upper_bound(p.var()).x;
    e.add(c);
    return true;
}

bool core:: explain_coeff_upper_bound(const lp::lar_term::ival& p, rational& bound, lp::explanation& e) const {
    const rational& a = p.coeff();
    SASSERT(!a.is_zero());
    unsigned c; // the index for the lower or the upper bound
    if (a.is_neg()) {
        unsigned c = m_lar_solver.get_column_lower_bound_witness(p.var());
        if (c + 1 == 0)
            return false;
        bound = a * m_lar_solver.get_lower_bound(p.var()).x;
        e.add(c);
        return true;
    }
    // a.is_pos()
    c = m_lar_solver.get_column_upper_bound_witness(p.var());
    if (c + 1 == 0)
        return false;
    bound = a * m_lar_solver.get_upper_bound(p.var()).x;
    e.add(c);
    return true;
}
    
// return true iff the negation of the ineq can be derived from the constraints
bool core:: explain_ineq(const lp::lar_term& t, llc cmp, const rational& rs) {
    // check that we have something like 0 < 0, which is always false and can be safely
    // removed from the lemma
        
    if (t.is_empty() && rs.is_zero() &&
        (cmp == llc::LT || cmp == llc::GT || cmp == llc::NE)) return true;
    lp::explanation exp;
    bool r;
    switch(negate(cmp)) {
    case llc::LE:
        r = explain_upper_bound(t, rs, exp);
        break;
    case llc::LT:
        r = explain_upper_bound(t, rs - rational(1), exp);
        break;
    case llc::GE: 
        r = explain_lower_bound(t, rs, exp);
        break;
    case llc::GT:
        r = explain_lower_bound(t, rs + rational(1), exp);
        break;

    case llc::EQ:
        r = (explain_lower_bound(t, rs, exp) && explain_upper_bound(t, rs, exp)) ||
            (rs.is_zero() && explain_by_equiv(t, exp));
        break;
    case llc::NE:
        r = explain_lower_bound(t, rs + rational(1), exp) || explain_upper_bound(t, rs - rational(1), exp);
            
            
        break;
    default:
        SASSERT(false);
        r = false;
    }
    if (r) {
        current_expl().add(exp);
        return true;
    }
        
    return false;
}

/**
 * \brief
 if t is an octagon term -+x -+ y try to explain why the term always
 equal zero
*/
bool core:: explain_by_equiv(const lp::lar_term& t, lp::explanation& e) {
    lpvar i,j;
    bool sign;
    if (!is_octagon_term(t, sign, i, j))
        return false;
    if (m_evars.find(signed_var(i,false)) != m_evars.find(signed_var(j, sign)))
        return false;
            
    m_evars.explain(signed_var(i,false), signed_var(j, sign), e);
    TRACE("nla_solver", tout << "explained :"; m_lar_solver.print_term(t, tout););
    return true;
            
}
    
void core::mk_ineq(lp::lar_term& t, llc cmp, const rational& rs) {
    TRACE("nla_solver_details", m_lar_solver.print_term(t, tout << "t = "););
    if (explain_ineq(t, cmp, rs)) {
        return;
    }
    m_lar_solver.subs_term_columns(t);
    current_lemma().push_back(ineq(cmp, t, rs));
    CTRACE("nla_solver", ineq_holds(ineq(cmp, t, rs)), print_ineq(ineq(cmp, t, rs), tout) << "\n";);
    SASSERT(!ineq_holds(ineq(cmp, t, rs)));
}

void core::mk_ineq(const rational& a, lpvar j, const rational& b, lpvar k, llc cmp, const rational& rs) {
    lp::lar_term t;
    t.add_coeff_var(a, j);
    t.add_coeff_var(b, k);
    mk_ineq(t, cmp, rs);
}

void core:: mk_ineq(lpvar j, const rational& b, lpvar k, llc cmp, const rational& rs) {
    mk_ineq(rational(1), j, b, k, cmp, rs);
}

void core:: mk_ineq(lpvar j, const rational& b, lpvar k, llc cmp) {
    mk_ineq(j, b, k, cmp, rational::zero());
}

void core:: mk_ineq(const rational& a, lpvar j, const rational& b, lpvar k, llc cmp) {
    mk_ineq(a, j, b, k, cmp, rational::zero());
}

void core:: mk_ineq(const rational& a ,lpvar j, lpvar k, llc cmp, const rational& rs) {
    mk_ineq(a, j, rational(1), k, cmp, rs);
}

void core:: mk_ineq(lpvar j, lpvar k, llc cmp, const rational& rs) {
    mk_ineq(j, rational(1), k, cmp, rs);
}

void core:: mk_ineq(lpvar j, llc cmp, const rational& rs) {
    mk_ineq(rational(1), j, cmp, rs);
}

void core:: mk_ineq(const rational& a, lpvar j, llc cmp, const rational& rs) {
    lp::lar_term t;        
    t.add_coeff_var(a, j);
    mk_ineq(t, cmp, rs);
}

llc negate(llc cmp) {
    switch(cmp) {
    case llc::LE: return llc::GT;
    case llc::LT: return llc::GE;
    case llc::GE: return llc::LT;
    case llc::GT: return llc::LE;
    case llc::EQ: return llc::NE;
    case llc::NE: return llc::EQ;
    default: SASSERT(false);
    };
    return cmp; // not reachable
}
    
llc apply_minus(llc cmp) {
    switch(cmp) {
    case llc::LE: return llc::GE;
    case llc::LT: return llc::GT;
    case llc::GE: return llc::LE;
    case llc::GT: return llc::LT;
    default: break;
    }
    return cmp;
}
    
void core:: mk_ineq(const rational& a, lpvar j, llc cmp) {
    mk_ineq(a, j, cmp, rational::zero());
}

void core:: mk_ineq(lpvar j, lpvar k, llc cmp, lemma& l) {
    mk_ineq(rational(1), j, rational(1), k, cmp, rational::zero());
}

void core:: mk_ineq(lpvar j, llc cmp) {
    mk_ineq(j, cmp, rational::zero());
}
    
// the monomials should be equal by modulo sign but this is not so in the model
void core:: fill_explanation_and_lemma_sign(const monomial& a, const monomial & b, rational const& sign) {
    SASSERT(sign == 1 || sign == -1);
    explain(a, current_expl());
    explain(b, current_expl());
    TRACE("nla_solver",
          tout << "used constraints: ";
          for (auto &p :  current_expl())
              m_lar_solver.print_constraint(p.second, tout); tout << "\n";
          );
    SASSERT(current_ineqs().size() == 0);
    mk_ineq(rational(1), a.var(), -sign, b.var(), llc::EQ, rational::zero());
    TRACE("nla_solver", print_lemma(tout););
}

// Replaces each variable index by the root in the tree and flips the sign if the var comes with a minus.
// Also sorts the result.
// 
svector<lpvar> core::reduce_monomial_to_rooted(const svector<lpvar> & vars, rational & sign) const {
    svector<lpvar> ret;
    bool s = false;
    for (lpvar v : vars) {
        auto root = m_evars.find(v);
        s ^= root.sign();
        TRACE("nla_solver_eq",
              print_var(v,tout);
              tout << " mapped to ";
              print_var(root.var(), tout););
        ret.push_back(root.var());
    }
    sign = rational(s? -1: 1);
    std::sort(ret.begin(), ret.end());
    return ret;
}


// Replaces definition m_v = v1* .. * vn by
// m_v = coeff * w1 * ... * wn, where w1, .., wn are canonical
// representatives, which are the roots of the equivalence tree, under current equations.
// 
monomial_coeff core::canonize_monomial(monomial const& m) const {
    rational sign = rational(1);
    svector<lpvar> vars = reduce_monomial_to_rooted(m.vars(), sign);
    return monomial_coeff(vars, sign);
}

lemma& core::current_lemma() { return m_lemma_vec->back(); }
const lemma& core::current_lemma() const { return m_lemma_vec->back(); }
vector<ineq>& core::current_ineqs() { return current_lemma().ineqs(); }
lp::explanation& core::current_expl() { return current_lemma().expl(); }
const lp::explanation& core::current_expl() const { return current_lemma().expl(); }


int core::vars_sign(const svector<lpvar>& v) {
    int sign = 1;
    for (lpvar j : v) {
        sign *= nla::rat_sign(vvr(j));
        if (sign == 0) 
            return 0;
    }
    return sign;
}

void core:: negate_strict_sign(lpvar j) {
    TRACE("nla_solver_details", print_var(j, tout););
    if (!vvr(j).is_zero()) {
        int sign = nla::rat_sign(vvr(j));
        mk_ineq(j, (sign == 1? llc::LE : llc::GE));
    } else {   // vvr(j).is_zero()
        if (has_lower_bound(j) && get_lower_bound(j) >= rational(0)) {
            explain_existing_lower_bound(j);
            mk_ineq(j, llc::GT);
        } else {
            SASSERT(has_upper_bound(j) && get_upper_bound(j) <= rational(0));
            explain_existing_upper_bound(j);
            mk_ineq(j, llc::LT);
        }
    }
}
    
void core:: generate_strict_case_zero_lemma(const monomial& m, unsigned zero_j, int sign_of_zj) {
    TRACE("nla_solver_bl", tout << "sign_of_zj = " << sign_of_zj << "\n";);
    // we know all the signs
    add_empty_lemma();
    mk_ineq(zero_j, (sign_of_zj == 1? llc::GT : llc::LT));
    for (unsigned j : m){
        if (j != zero_j) {
            negate_strict_sign(j);
        }
    }
    negate_strict_sign(m.var());
    TRACE("nla_solver", print_lemma(tout););
}

bool core:: has_upper_bound(lpvar j) const {
    return m_lar_solver.column_has_upper_bound(j);
} 

bool core:: has_lower_bound(lpvar j) const {
    return m_lar_solver.column_has_lower_bound(j);
} 
const rational& core::get_upper_bound(unsigned j) const {
    return m_lar_solver.get_upper_bound(j).x;
}

const rational& core::get_lower_bound(unsigned j) const {
    return m_lar_solver.get_lower_bound(j).x;
}
    
    
bool core::zero_is_an_inner_point_of_bounds(lpvar j) const {
    if (has_upper_bound(j) && get_upper_bound(j) <= rational(0))            
        return false;
    if (has_lower_bound(j) && get_lower_bound(j) >= rational(0))            
        return false;
    return true;
}
    

bool core:: try_get_non_strict_sign_from_bounds(lpvar j, int& sign) const {
    SASSERT(sign);
    if (has_lower_bound(j) && get_lower_bound(j) >= rational(0))
        return true;
    if (has_upper_bound(j) && get_upper_bound(j) <= rational(0)) {
        sign = -sign;
        return true;
    }
    sign = 0;
    return false;
}

void core:: get_non_strict_sign(lpvar j, int& sign) const {
    const rational & v = vvr(j);
    if (v.is_zero()) {
        try_get_non_strict_sign_from_bounds(j, sign);
    } else {
        sign *= nla::rat_sign(v);
    }
}


void core:: add_trival_zero_lemma(lpvar zero_j, const monomial& m) {
    add_empty_lemma();
    mk_ineq(zero_j, llc::NE);
    mk_ineq(m.var(), llc::EQ);
    TRACE("nla_solver", print_lemma(tout););            
}
    
void core:: generate_zero_lemmas(const monomial& m) {
    SASSERT(!vvr(m).is_zero() && product_value(m).is_zero());
    int sign = nla::rat_sign(vvr(m));
    unsigned_vector fixed_zeros;
    lpvar zero_j = find_best_zero(m, fixed_zeros);
    SASSERT(is_set(zero_j));
    unsigned zero_power = 0;
    for (unsigned j : m){
        if (j == zero_j) {
            zero_power++;
            continue;
        }
        get_non_strict_sign(j, sign);
        if(sign == 0)
            break;
    }
    if (sign && is_even(zero_power))
        sign = 0;
    TRACE("nla_solver_details", tout << "zero_j = " << zero_j << ", sign = " << sign << "\n";);
    if (sign == 0) { // have to generate a non-convex lemma
        add_trival_zero_lemma(zero_j, m);
    } else { 
        generate_strict_case_zero_lemma(m, zero_j, sign);
    }
    for (lpvar j : fixed_zeros)
        add_fixed_zero_lemma(m, j);
}

void core:: add_fixed_zero_lemma(const monomial& m, lpvar j) {
    add_empty_lemma();
    explain_fixed_var(j);
    mk_ineq(m.var(), llc::EQ);
    TRACE("nla_solver", print_lemma(tout););
}

llc core::negate(llc cmp) {
    switch(cmp) {
    case llc::LE: return llc::GT;
    case llc::LT: return llc::GE;
    case llc::GE: return llc::LT;
    case llc::GT: return llc::LE;
    case llc::EQ: return llc::NE;
    case llc::NE: return llc::EQ;
    default: SASSERT(false);
    };
    return cmp; // not reachable
}

int core::rat_sign(const monomial& m) const {
    int sign = 1;
    for (lpvar j : m) {
        auto v = vvr(j);
        if (v.is_neg()) {
            sign = - sign;
            continue;
        }
        if (v.is_pos()) {
            continue;
        }
        sign = 0;
        break;
    }
    return sign;
}

// Returns true if the monomial sign is incorrect
bool core:: sign_contradiction(const monomial& m) const {
    return  nla::rat_sign(vvr(m)) != rat_sign(m);
}

/*
  unsigned_vector eq_vars(lpvar j) const {
  TRACE("nla_solver_eq", tout << "j = "; print_var(j, tout); tout << "eqs = ";
  for(auto jj : m_evars.eq_vars(j)) {
  print_var(jj, tout);
  });
  return m_evars.eq_vars(j);
  }
*/

// Monomials m and n vars have the same values, up to "sign"
// Generate a lemma if values of m.var() and n.var() are not the same up to sign
bool core:: basic_sign_lemma_on_two_monomials(const monomial& m, const monomial& n, const rational& sign) {
    if (vvr(m) == vvr(n) *sign)
        return false;
    TRACE("nla_solver", tout << "sign contradiction:\nm = "; print_monomial_with_vars(m, tout); tout << "n= "; print_monomial_with_vars(n, tout); tout << "sign: " << sign << "\n";);
    generate_sign_lemma(m, n, sign);
    return true;
}

void core:: basic_sign_lemma_model_based_one_mon(const monomial& m, int product_sign) {
    if (product_sign == 0) {
        TRACE("nla_solver_bl", tout << "zero product sign\n";);
        generate_zero_lemmas(m);
    } else {
        add_empty_lemma();
        for(lpvar j: m) {
            negate_strict_sign(j);
        }
        mk_ineq(m.var(), product_sign == 1? llc::GT : llc::LT);
        TRACE("nla_solver", print_lemma(tout); tout << "\n";);
    }
}
    
bool core:: basic_sign_lemma_model_based() {
    unsigned i = random() % m_to_refine.size();
    unsigned ii = i;
    do {
        const monomial& m = m_monomials[m_to_refine[i]];
        int mon_sign = nla::rat_sign(vvr(m));
        int product_sign = rat_sign(m);
        if (mon_sign != product_sign) {
            basic_sign_lemma_model_based_one_mon(m, product_sign);
            if (done())
                return true;
        }
        i++;
        if (i == m_to_refine.size())
            i = 0;
    } while (i != ii);
    return m_lemma_vec->size() > 0;
}

    
bool core:: basic_sign_lemma_on_mon(unsigned i, std::unordered_set<unsigned> & explored){
    const monomial& m = m_monomials[i];
    TRACE("nla_solver_details", tout << "i = " << i << ", mon = "; print_monomial_with_vars(m, tout););
    const index_with_sign&  rm_i_s = m_rm_table.get_rooted_mon(i);
    unsigned k = rm_i_s.index();
    if (!try_insert(k, explored))
        return false;

    const auto& mons_to_explore = m_rm_table.rms()[k].m_mons;
    TRACE("nla_solver", tout << "rm = "; print_rooted_monomial_with_vars(m_rm_table.rms()[k], tout) << "\n";);
        
    for (index_with_sign i_s : mons_to_explore) {
        TRACE("nla_solver", tout << "i_s = (" << i_s.index() << "," << i_s.sign() << ")\n";
              print_monomial_with_vars(m_monomials[i_s.index()], tout << "m = ") << "\n";
              {
                  for (lpvar j : m_monomials[i_s.index()] ) {
                      lpvar rj = m_evars.find(j).var();
                      if (j == rj)
                          tout << "rj = j ="  << j << "\n";
                      else {
                          lp::explanation e;
                          m_evars.explain(j, e);
                          tout << "j = " << j << ", e = "; print_explanation(e, tout) << "\n";
                      }
                  }
              }
              );
        unsigned n = i_s.index();
        if (n == i) continue;
        if (basic_sign_lemma_on_two_monomials(m, m_monomials[n], rm_i_s.sign()*i_s.sign()))
            if(done())
                return true;
    }
    TRACE("nla_solver_details", tout << "return false\n";);
    return false;
}

/**
 * \brief <generate lemma by using the fact that -ab = (-a)b) and
 -ab = a(-b)
*/
bool core:: basic_sign_lemma(bool derived) {
    if (!derived)
        return basic_sign_lemma_model_based();

    std::unordered_set<unsigned> explored;
    for (unsigned i : m_to_refine){
        if (basic_sign_lemma_on_mon(i, explored))
            return true;
    }
    return false;
}

bool core:: var_is_fixed_to_zero(lpvar j) const {
    return 
        m_lar_solver.column_has_upper_bound(j) &&
        m_lar_solver.column_has_lower_bound(j) &&
        m_lar_solver.get_upper_bound(j) == lp::zero_of_type<lp::impq>() &&
        m_lar_solver.get_lower_bound(j) == lp::zero_of_type<lp::impq>();
}
bool core:: var_is_fixed_to_val(lpvar j, const rational& v) const {
    return 
        m_lar_solver.column_has_upper_bound(j) &&
        m_lar_solver.column_has_lower_bound(j) &&
        m_lar_solver.get_upper_bound(j) == lp::impq(v) && m_lar_solver.get_lower_bound(j) == lp::impq(v);
}

bool core:: var_is_fixed(lpvar j) const {
    return 
        m_lar_solver.column_has_upper_bound(j) &&
        m_lar_solver.column_has_lower_bound(j) &&
        m_lar_solver.get_upper_bound(j) == m_lar_solver.get_lower_bound(j);
}
    
std::ostream & core::print_ineq(const ineq & in, std::ostream & out) const {
    m_lar_solver.print_term(in.m_term, out);
    out << " " << lconstraint_kind_string(in.m_cmp) << " " << in.m_rs;
    return out;
}

std::ostream & core::print_var(lpvar j, std::ostream & out) const {
    auto it = m_var_to_its_monomial.find(j);
    if (it != m_var_to_its_monomial.end()) {
        print_monomial(m_monomials[it->second], out);
        out << " = " << vvr(j);;
    }
        
    m_lar_solver.print_column_info(j, out) <<";";
    return out;
}

std::ostream & core::print_monomials(std::ostream & out) const {
    for (auto &m : m_monomials) {
        print_monomial_with_vars(m, out);
    }
    return out;
}    

std::ostream & core::print_ineqs(const lemma& l, std::ostream & out) const {
    std::unordered_set<lpvar> vars;
    out << "ineqs: ";
    if (l.ineqs().size() == 0) {
        out << "conflict\n";
    } else {
        for (unsigned i = 0; i < l.ineqs().size(); i++) {
            auto & in = l.ineqs()[i]; 
            print_ineq(in, out);
            if (i + 1 < l.ineqs().size()) out << " or ";
            for (const auto & p: in.m_term)
                vars.insert(p.var());
        }
        out << std::endl;
        for (lpvar j : vars) {
            print_var(j, out);
        }
        out << "\n";
    }
    return out;
}
    
std::ostream & core::print_factorization(const factorization& f, std::ostream& out) const {
    if (f.is_mon()){
        print_monomial(*f.mon(), out << "is_mon ");
    } else {
        for (unsigned k = 0; k < f.size(); k++ ) {
            print_factor(f[k], out);
            if (k < f.size() - 1)
                out << "*";
        }
    }
    return out;
}
    
bool core:: find_rm_monomial_of_vars(const svector<lpvar>& vars, unsigned & i) const {
    SASSERT(vars_are_roots(vars));
    auto it = m_rm_table.vars_key_to_rm_index().find(vars);
    if (it == m_rm_table.vars_key_to_rm_index().end()) {
        return false;
    }
        
    i = it->second;
    TRACE("nla_solver",);
        
    SASSERT(lp::vectors_are_equal_(vars, m_rm_table.rms()[i].vars()));
    return true;
}

const monomial* core::find_monomial_of_vars(const svector<lpvar>& vars) const {
    unsigned i;
    if (!find_rm_monomial_of_vars(vars, i))
        return nullptr;
    return &m_monomials[m_rm_table.rms()[i].orig_index()];
}


void core::explain_existing_lower_bound(lpvar j) {
    SASSERT(has_lower_bound(j));
    current_expl().add(m_lar_solver.get_column_lower_bound_witness(j));
}

void core::explain_existing_upper_bound(lpvar j) {
    SASSERT(has_upper_bound(j));
    current_expl().add(m_lar_solver.get_column_upper_bound_witness(j));
}
    
void core::explain_separation_from_zero(lpvar j) {
    SASSERT(!vvr(j).is_zero());
    if (vvr(j).is_pos())
        explain_existing_lower_bound(j);
    else
        explain_existing_upper_bound(j);
}

int core::get_derived_sign(const rooted_mon& rm, const factorization& f) const {
    rational sign = rm.orig_sign(); // this is the flip sign of the variable var(rm)
    SASSERT(!sign.is_zero());
    for (const factor& fc: f) {
        lpvar j = var(fc);
        if (!(var_has_positive_lower_bound(j) || var_has_negative_upper_bound(j))) {
            return 0;
        }
        sign *= m_evars.find(j).rsign();
    }
    return nla::rat_sign(sign);
}
void core::trace_print_monomial_and_factorization(const rooted_mon& rm, const factorization& f, std::ostream& out) const {
    out << "rooted vars: ";
    print_product(rm.m_vars, out);
    out << "\n";
        
    print_monomial(rm.orig_index(), out << "mon:  ") << "\n";
    out << "value: " << vvr(rm) << "\n";
    print_factorization(f, out << "fact: ") << "\n";
}

void core::explain_var_separated_from_zero(lpvar j) {
    SASSERT(var_is_separated_from_zero(j));
    if (m_lar_solver.column_has_upper_bound(j) && (m_lar_solver.get_upper_bound(j)< lp::zero_of_type<lp::impq>())) 
        current_expl().add(m_lar_solver.get_column_upper_bound_witness(j));
    else 
        current_expl().add(m_lar_solver.get_column_lower_bound_witness(j));
}

void core::explain_fixed_var(lpvar j) {
    SASSERT(var_is_fixed(j));
    current_expl().add(m_lar_solver.get_column_upper_bound_witness(j));
    current_expl().add(m_lar_solver.get_column_lower_bound_witness(j));
}

bool core:: var_has_positive_lower_bound(lpvar j) const {
    return m_lar_solver.column_has_lower_bound(j) && m_lar_solver.get_lower_bound(j) > lp::zero_of_type<lp::impq>();
}

bool core:: var_has_negative_upper_bound(lpvar j) const {
    return m_lar_solver.column_has_upper_bound(j) && m_lar_solver.get_upper_bound(j) < lp::zero_of_type<lp::impq>();
}
    
bool core:: var_is_separated_from_zero(lpvar j) const {
    return
        var_has_negative_upper_bound(j) ||
        var_has_positive_lower_bound(j);
}
    
// x = 0 or y = 0 -> xy = 0
bool core:: basic_lemma_for_mon_non_zero_derived(const rooted_mon& rm, const factorization& f) {
    TRACE("nla_solver", trace_print_monomial_and_factorization(rm, f, tout););
    if (!var_is_separated_from_zero(var(rm)))
        return false; 
    int zero_j = -1;
    for (auto j : f) {
        if (var_is_fixed_to_zero(var(j))) {
            zero_j = var(j);
            break;
        }
    }

    if (zero_j == -1) {
        return false;
    } 
    add_empty_lemma();
    explain_fixed_var(zero_j);
    explain_var_separated_from_zero(var(rm));
    explain(rm, current_expl());
    TRACE("nla_solver", print_lemma(tout););
    return true;
}

// use the fact that
// |xabc| = |x| and x != 0 -> |a| = |b| = |c| = 1 
bool core:: basic_lemma_for_mon_neutral_monomial_to_factor_model_based(const rooted_mon& rm, const factorization& f) {
    TRACE("nla_solver_bl", trace_print_monomial_and_factorization(rm, f, tout););

    lpvar mon_var = m_monomials[rm.orig_index()].var();
    TRACE("nla_solver_bl", trace_print_monomial_and_factorization(rm, f, tout); tout << "\nmon_var = " << mon_var << "\n";);
        
    const auto & mv = vvr(mon_var);
    const auto  abs_mv = abs(mv);
        
    if (abs_mv == rational::zero()) {
        return false;
    }
    lpvar jl = -1;
    for (auto j : f ) {
        if (abs(vvr(j)) == abs_mv) {
            jl = var(j);
            break;
        }
    }
    if (jl == static_cast<lpvar>(-1))
        return false;
    lpvar not_one_j = -1;
    for (auto j : f ) {
        if (var(j) == jl) {
            continue;
        }
        if (abs(vvr(j)) != rational(1)) {
            not_one_j = var(j);
            break;
        }
    }

    if (not_one_j == static_cast<lpvar>(-1)) {
        return false;
    } 

    add_empty_lemma();
    // mon_var = 0
    mk_ineq(mon_var, llc::EQ);
        
    // negate abs(jl) == abs()
    if (vvr(jl) == - vvr(mon_var))
        mk_ineq(jl, mon_var, llc::NE, current_lemma());   
    else  // jl == mon_var
        mk_ineq(jl, -rational(1), mon_var, llc::NE);   

    // not_one_j = 1
    mk_ineq(not_one_j, llc::EQ,  rational(1));   
        
    // not_one_j = -1
    mk_ineq(not_one_j, llc::EQ, -rational(1));
    explain(rm, current_expl());
    explain(f, current_expl());

    TRACE("nla_solver", print_lemma(tout); );
    return true;
}
// use the fact that
// |xabc| = |x| and x != 0 -> |a| = |b| = |c| = 1 
bool core:: basic_lemma_for_mon_neutral_monomial_to_factor_model_based_fm(const monomial& m) {
    TRACE("nla_solver_bl", print_monomial(m, tout););

    lpvar mon_var = m.var();
    const auto & mv = vvr(mon_var);
    const auto  abs_mv = abs(mv);
    if (abs_mv == rational::zero()) {
        return false;
    }
    lpvar jl = -1;
    for (auto j : m ) {
        if (abs(vvr(j)) == abs_mv) {
            jl = j;
            break;
        }
    }
    if (jl == static_cast<lpvar>(-1))
        return false;
    lpvar not_one_j = -1;
    for (auto j : m ) {
        if (j == jl) {
            continue;
        }
        if (abs(vvr(j)) != rational(1)) {
            not_one_j = j;
            break;
        }
    }

    if (not_one_j == static_cast<lpvar>(-1)) {
        return false;
    } 

    add_empty_lemma();
    // mon_var = 0
    mk_ineq(mon_var, llc::EQ);
        
    // negate abs(jl) == abs()
    if (vvr(jl) == - vvr(mon_var))
        mk_ineq(jl, mon_var, llc::NE, current_lemma());   
    else  // jl == mon_var
        mk_ineq(jl, -rational(1), mon_var, llc::NE);   

    // not_one_j = 1
    mk_ineq(not_one_j, llc::EQ,  rational(1));   
        
    // not_one_j = -1
    mk_ineq(not_one_j, llc::EQ, -rational(1));
    TRACE("nla_solver", print_lemma(tout); );
    return true;
}

bool core:: vars_are_equiv(lpvar a, lpvar b) const {
    SASSERT(abs(vvr(a)) == abs(vvr(b)));
    return m_evars.vars_are_equiv(a, b);
}

    
void core::explain_equiv_vars(lpvar a, lpvar b) {
    SASSERT(abs(vvr(a)) == abs(vvr(b)));
    if (m_evars.vars_are_equiv(a, b)) {
        explain(a, current_expl());
        explain(b, current_expl());
    } else {
        explain_fixed_var(a);
        explain_fixed_var(b);
    }
}

// use the fact that
// |xabc| = |x| and x != 0 -> |a| = |b| = |c| = 1 
bool core:: basic_lemma_for_mon_neutral_monomial_to_factor_derived(const rooted_mon& rm, const factorization& f) {
    TRACE("nla_solver", trace_print_monomial_and_factorization(rm, f, tout););

    lpvar mon_var = m_monomials[rm.orig_index()].var();
    TRACE("nla_solver", trace_print_monomial_and_factorization(rm, f, tout); tout << "\nmon_var = " << mon_var << "\n";);
        
    const auto & mv = vvr(mon_var);
    const auto  abs_mv = abs(mv);
        
    if (abs_mv == rational::zero()) {
        return false;
    }
    bool mon_var_is_sep_from_zero = var_is_separated_from_zero(mon_var);
    lpvar jl = -1;
    for (auto fc : f ) {
        lpvar j = var(fc);
        if (abs(vvr(j)) == abs_mv && vars_are_equiv(j, mon_var) &&
            (mon_var_is_sep_from_zero || var_is_separated_from_zero(j))) {
            jl = j;
            break;
        }
    }
    if (jl == static_cast<lpvar>(-1))
        return false;
        
    lpvar not_one_j = -1;
    for (auto j : f ) {
        if (var(j) == jl) {
            continue;
        }
        if (abs(vvr(j)) != rational(1)) {
            not_one_j = var(j);
            break;
        }
    }

    if (not_one_j == static_cast<lpvar>(-1)) {
        return false;
    } 

    add_empty_lemma();
    // mon_var = 0
    if (mon_var_is_sep_from_zero)
        explain_var_separated_from_zero(mon_var);
    else
        explain_var_separated_from_zero(jl);

    explain_equiv_vars(mon_var, jl);
        
    // not_one_j = 1
    mk_ineq(not_one_j, llc::EQ,  rational(1));   
        
    // not_one_j = -1
    mk_ineq(not_one_j, llc::EQ, -rational(1));
    explain(rm, current_expl());
    TRACE("nla_solver", print_lemma(tout); );
    return true;
}

// use the fact
// 1 * 1 ... * 1 * x * 1 ... * 1 = x
bool core:: basic_lemma_for_mon_neutral_from_factors_to_monomial_model_based(const rooted_mon& rm, const factorization& f) {
    rational sign = rm.orig_sign();
    TRACE("nla_solver_bl", tout << "f = "; print_factorization(f, tout); tout << ", sign = " << sign << '\n'; );
    lpvar not_one = -1;
    for (auto j : f){
        TRACE("nla_solver_bl", tout << "j = "; print_factor_with_vars(j, tout););
        auto v = vvr(j);
        if (v == rational(1)) {
            continue;
        }
            
        if (v == -rational(1)) { 
            sign = - sign;
            continue;
        } 
            
        if (not_one == static_cast<lpvar>(-1)) {
            not_one = var(j);
            continue;
        }
            
        // if we are here then there are at least two factors with absolute values different from one : cannot create the lemma
        return false;
    }

    if (not_one + 1) {
        // we found the only not_one
        if (vvr(rm) == vvr(not_one) * sign) {
            TRACE("nla_solver", tout << "the whole equal to the factor" << std::endl;);
            return false;
        }
    } else {
        // we have +-ones only in the factorization
        if (vvr(rm) == sign) {
            return false;
        }
    }

    TRACE("nla_solver_bl", tout << "not_one = " << not_one << "\n";);
        
    add_empty_lemma();

    for (auto j : f){
        lpvar var_j = var(j);
        if (not_one == var_j) continue;
        mk_ineq(var_j, llc::NE, j.is_var()? vvr(j) : canonize_sign(j) * vvr(j));   
    }

    if (not_one == static_cast<lpvar>(-1)) {
        mk_ineq(m_monomials[rm.orig_index()].var(), llc::EQ, sign);
    } else {
        mk_ineq(m_monomials[rm.orig_index()].var(), -sign, not_one, llc::EQ);
    }
    explain(rm, current_expl());
    explain(f, current_expl());
    TRACE("nla_solver",
          print_lemma(tout);
          tout << "rm = "; print_rooted_monomial_with_vars(rm, tout);
          );
    return true;
}
// use the fact
// 1 * 1 ... * 1 * x * 1 ... * 1 = x
bool core:: basic_lemma_for_mon_neutral_from_factors_to_monomial_model_based_fm(const monomial& m) {
    lpvar not_one = -1;
    rational sign(1);
    TRACE("nla_solver_bl", tout << "m = "; print_monomial(m, tout););
    for (auto j : m){
        auto v = vvr(j);
        if (v == rational(1)) {
            continue;
        }
        if (v == -rational(1)) { 
            sign = - sign;
            continue;
        } 
        if (not_one == static_cast<lpvar>(-1)) {
            not_one = j;
            continue;
        }
        // if we are here then there are at least two factors with values different from one and minus one: cannot create the lemma
        return false;
    }

    if (not_one + 1) {  // we found the only not_one
        if (vvr(m) == vvr(not_one) * sign) {
            TRACE("nla_solver", tout << "the whole equal to the factor" << std::endl;);
            return false;
        }
    }
        
    add_empty_lemma();
    for (auto j : m){
        if (not_one == j) continue;
        mk_ineq(j, llc::NE, vvr(j));   
    }

    if (not_one == static_cast<lpvar>(-1)) {
        mk_ineq(m.var(), llc::EQ, sign);
    } else {
        mk_ineq(m.var(), -sign, not_one, llc::EQ);
    }
    TRACE("nla_solver",  print_lemma(tout););
    return true;
}
// use the fact
// 1 * 1 ... * 1 * x * 1 ... * 1 = x
bool core:: basic_lemma_for_mon_neutral_from_factors_to_monomial_derived(const rooted_mon& rm, const factorization& f) {
    return false;
    rational sign = rm.orig().m_sign;
    lpvar not_one = -1;

    TRACE("nla_solver", tout << "f = "; print_factorization(f, tout););
    for (auto j : f){
        TRACE("nla_solver", tout << "j = "; print_factor_with_vars(j, tout););
        auto v = vvr(j);
        if (v == rational(1)) {
            continue;
        }

        if (v == -rational(1)) { 
            sign = - sign;
            continue;
        } 

        if (not_one == static_cast<lpvar>(-1)) {
            not_one = var(j);
            continue;
        }

        // if we are here then there are at least two factors with values different from one and minus one: cannot create the lemma
        return false;
    }

    add_empty_lemma();
    explain(rm, current_expl());

    for (auto j : f){
        lpvar var_j = var(j);
        if (not_one == var_j) continue;
        mk_ineq(var_j, llc::NE, j.is_var()? vvr(j) : canonize_sign(j) * vvr(j));   
    }

    if (not_one == static_cast<lpvar>(-1)) {
        mk_ineq(m_monomials[rm.orig_index()].var(), llc::EQ, sign);
    } else {
        mk_ineq(m_monomials[rm.orig_index()].var(), -sign, not_one, llc::EQ);
    }
    TRACE("nla_solver",
          tout << "rm = "; print_rooted_monomial_with_vars(rm, tout);
          print_lemma(tout););
    return true;
}
void core::basic_lemma_for_mon_neutral_model_based(const rooted_mon& rm, const factorization& f) {
    if (f.is_mon()) {
        basic_lemma_for_mon_neutral_monomial_to_factor_model_based_fm(*f.mon());
        basic_lemma_for_mon_neutral_from_factors_to_monomial_model_based_fm(*f.mon());
    }
    else {
        basic_lemma_for_mon_neutral_monomial_to_factor_model_based(rm, f);
        basic_lemma_for_mon_neutral_from_factors_to_monomial_model_based(rm, f);
    }
}
    
bool core:: basic_lemma_for_mon_neutral_derived(const rooted_mon& rm, const factorization& factorization) {
    return
        basic_lemma_for_mon_neutral_monomial_to_factor_derived(rm, factorization) || 
        basic_lemma_for_mon_neutral_from_factors_to_monomial_derived(rm, factorization);
    return false;
}

void core::explain(const factorization& f, lp::explanation& exp) {
    SASSERT(!f.is_mon());
    for (const auto& fc : f) {
        explain(fc, exp);
    }
}

bool core:: has_zero_factor(const factorization& factorization) const {
    for (factor f : factorization) {
        if (vvr(f).is_zero())
            return true;
    }
    return false;
}

template <typename T>
bool core:: has_zero(const T& product) const {
    for (const rational & t : product) {
        if (t.is_zero())
            return true;
    }
    return false;
}

template <typename T>
bool core:: mon_has_zero(const T& product) const {
    for (lpvar j: product) {
        if (vvr(j).is_zero())
            return true;
    }
    return false;
}

// x != 0 or y = 0 => |xy| >= |y|
void core::proportion_lemma_model_based(const rooted_mon& rm, const factorization& factorization) {
    rational rmv = abs(vvr(rm));
    if (rmv.is_zero()) {
        SASSERT(has_zero_factor(factorization));
        return;
    }
    int factor_index = 0;
    for (factor f : factorization) {
        if (abs(vvr(f)) > rmv) {
            generate_pl(rm, factorization, factor_index);
            return;
        }
        factor_index++;
    }
}
// x != 0 or y = 0 => |xy| >= |y|
bool core:: proportion_lemma_derived(const rooted_mon& rm, const factorization& factorization) {
    return false;
    rational rmv = abs(vvr(rm));
    if (rmv.is_zero()) {
        SASSERT(has_zero_factor(factorization));
        return false;
    }
    int factor_index = 0;
    for (factor f : factorization) {
        if (abs(vvr(f)) > rmv) {
            generate_pl(rm, factorization, factor_index);
            return true;
        }
        factor_index++;
    }
    return false;
}

void core::basic_lemma_for_mon_model_based(const rooted_mon& rm) {
    TRACE("nla_solver_bl", tout << "rm = "; print_rooted_monomial(rm, tout););
    if (vvr(rm).is_zero()) {
        for (auto factorization : factorization_factory_imp(rm, *this)) {
            if (factorization.is_empty())
                continue;
            basic_lemma_for_mon_zero_model_based(rm, factorization);
            basic_lemma_for_mon_neutral_model_based(rm, factorization);
        }
    } else {
        for (auto factorization : factorization_factory_imp(rm, *this)) {
            if (factorization.is_empty())
                continue;
            basic_lemma_for_mon_non_zero_model_based(rm, factorization);
            basic_lemma_for_mon_neutral_model_based(rm, factorization);
            proportion_lemma_model_based(rm, factorization) ;
        }
    }
}

bool core:: basic_lemma_for_mon_derived(const rooted_mon& rm) {
    if (var_is_fixed_to_zero(var(rm))) {
        for (auto factorization : factorization_factory_imp(rm, *this)) {
            if (factorization.is_empty())
                continue;
            if (basic_lemma_for_mon_zero(rm, factorization) ||
                basic_lemma_for_mon_neutral_derived(rm, factorization)) {
                explain(factorization, current_expl());
                return true;
            }
        }
    } else {
        for (auto factorization : factorization_factory_imp(rm, *this)) {
            if (factorization.is_empty())
                continue;
            if (basic_lemma_for_mon_non_zero_derived(rm, factorization) ||
                basic_lemma_for_mon_neutral_derived(rm, factorization) ||
                proportion_lemma_derived(rm, factorization)) {
                explain(factorization, current_expl());
                return true;
            }
        }
    }
    return false;
}
    
// Use basic multiplication properties to create a lemma
// for the given monomial.
// "derived" means derived from constraints - the alternative is model based
void core::basic_lemma_for_mon(const rooted_mon& rm, bool derived) {
    if (derived)
        basic_lemma_for_mon_derived(rm);
    else
        basic_lemma_for_mon_model_based(rm);
}

void core::init_rm_to_refine() {
    if (!m_rm_table.to_refine().empty())
        return;
    std::unordered_set<unsigned> ref;
    ref.insert(m_to_refine.begin(), m_to_refine.end());
    m_rm_table.init_to_refine(ref);
}

lp::lp_settings& core::settings() {
    return m_lar_solver.settings();
}
    
unsigned core::random() { return settings().random_next(); }
    
// use basic multiplication properties to create a lemma
bool core:: basic_lemma(bool derived) {
    if (basic_sign_lemma(derived))
        return true;
    if (derived)
        return false;
    init_rm_to_refine();
    const auto& rm_ref = m_rm_table.to_refine();
    TRACE("nla_solver", tout << "rm_ref = "; print_vector(rm_ref, tout););
    unsigned start = random() % rm_ref.size();
    unsigned i = start;
    do {
        const rooted_mon& r = m_rm_table.rms()[rm_ref[i]];
        SASSERT (!check_monomial(m_monomials[r.orig_index()]));
        basic_lemma_for_mon(r, derived);
        if (++i == rm_ref.size()) {
            i = 0;
        }
    } while(i != start && !done());
        
    return false;
}

void core::map_monomial_vars_to_monomial_indices(unsigned i) {
    const monomial& m = m_monomials[i];
    for (lpvar j : m.vars()) {
        auto it = m_monomials_containing_var.find(j);
        if (it == m_monomials_containing_var.end()) {
            unsigned_vector ms;
            ms.push_back(i);
            m_monomials_containing_var[j] = ms;
        }
        else {
            it->second.push_back(i);
        }
    }
}

void core::map_vars_to_monomials() {
    for (unsigned i = 0; i < m_monomials.size(); i++) {
        const monomial& m = m_monomials[i];
        lpvar v = m.var();
        SASSERT(m_var_to_its_monomial.find(v) == m_var_to_its_monomial.end());
        m_var_to_its_monomial[v] = i;
        map_monomial_vars_to_monomial_indices(i);
    }
}

// we look for octagon constraints here, with a left part  +-x +- y 
void core::collect_equivs() {
    const lp::lar_solver& s = m_lar_solver;

    for (unsigned i = 0; i < s.terms().size(); i++) {
        unsigned ti = i + s.terms_start_index();
        if (!s.term_is_used_as_row(ti))
            continue;
        lpvar j = s.external_to_local(ti);
        if (var_is_fixed_to_zero(j)) {
            TRACE("nla_solver_eq", tout << "term = "; s.print_term(*s.terms()[i], tout););
            add_equivalence_maybe(s.terms()[i], s.get_column_upper_bound_witness(j), s.get_column_lower_bound_witness(j));
        }
    }
}

void core::collect_equivs_of_fixed_vars() {
    std::unordered_map<rational, svector<lpvar> > abs_map;
    for (lpvar j = 0; j < m_lar_solver.number_of_vars(); j++) {
        if (!var_is_fixed(j))
            continue;
        rational v = abs(vvr(j));
        auto it = abs_map.find(v);
        if (it == abs_map.end()) {
            abs_map[v] = svector<lpvar>();
            abs_map[v].push_back(j);
        } else {
            it->second.push_back(j);
        }
    }
    for (auto p : abs_map) {
        svector<lpvar>& v = p.second;
        lpvar head = v[0];
        auto c0 = m_lar_solver.get_column_upper_bound_witness(head);
        auto c1 = m_lar_solver.get_column_lower_bound_witness(head);
        for (unsigned k = 1; k < v.size(); k++) {
            auto c2 = m_lar_solver.get_column_upper_bound_witness(v[k]);
            auto c3 = m_lar_solver.get_column_lower_bound_witness(v[k]);
            if (vvr(head) == vvr(v[k])) {
                m_evars.merge_plus(head, v[k], eq_justification({c0, c1, c2, c3}));
            } else {
                SASSERT(vvr(head) == -vvr(v[k]));
                m_evars.merge_minus(head, v[k], eq_justification({c0, c1, c2, c3}));
            }
        }
    }
}

// returns true iff the term is in a form +-x-+y.
// the sign is true iff the term is x+y, -x-y.
bool core:: is_octagon_term(const lp::lar_term& t, bool & sign, lpvar& i, lpvar &j) const {
    if (t.size() != 2)
        return false;
    bool seen_minus = false;
    bool seen_plus = false;
    i = -1;
    for(const auto & p : t) {
        const auto & c = p.coeff();
        if (c == 1) {
            seen_plus = true;
        } else if (c == - 1) {
            seen_minus = true;
        } else {
            return false;
        }
        if (i == static_cast<lpvar>(-1))
            i = p.var();
        else
            j = p.var();
    }
    SASSERT(j != static_cast<unsigned>(-1));
    sign = (seen_minus && seen_plus)? false : true;
    return true;
}
    
void core::add_equivalence_maybe(const lp::lar_term *t, lpci c0, lpci c1) {
    bool sign;
    lpvar i, j;
    if (!is_octagon_term(*t, sign, i, j))
        return;
    if (sign)
        m_evars.merge_minus(i, j, eq_justification({c0, c1}));
    else 
        m_evars.merge_plus(i, j, eq_justification({c0, c1}));
}

// x is equivalent to y if x = +- y
void core::init_vars_equivalence() {
    /*       SASSERT(m_evars.empty());*/
    collect_equivs();
    /*        TRACE("nla_solver_details", tout << "number of equivs = " << m_evars.size(););*/
        
    SASSERT((settings().random_next() % 100) || tables_are_ok());
}

bool core:: vars_table_is_ok() const {
    for (lpvar j = 0; j < m_lar_solver.number_of_vars(); j++) {
        auto it = m_var_to_its_monomial.find(j);
        if (it != m_var_to_its_monomial.end()
            && m_monomials[it->second].var() != j) {
            TRACE("nla_solver", tout << "j = ";
                  print_var(j, tout););
            return false;
        }
    }
    for (unsigned i = 0; i < m_monomials.size(); i++){
        const monomial& m = m_monomials[i];
        lpvar j = m.var();
        if (m_var_to_its_monomial.find(j) == m_var_to_its_monomial.end()){
            return false;
        }
    }
    return true;
}

bool core:: rm_table_is_ok() const {
    for (const auto & rm : m_rm_table.rms()) {
        for (lpvar j : rm.vars()) {
            if (!m_evars.is_root(j)){
                TRACE("nla_solver", print_var(j, tout););
                return false;
            }
        }
    }
    return true;
}
    
bool core:: tables_are_ok() const {
    return vars_table_is_ok() && rm_table_is_ok();
}
    
bool core:: var_is_a_root(lpvar j) const { return m_evars.is_root(j); }

template <typename T>
bool core:: vars_are_roots(const T& v) const {
    for (lpvar j: v) {
        if (!var_is_a_root(j))
            return false;
    }
    return true;
}


void core::register_monomial_in_tables(unsigned i_mon) {
    const monomial& m = m_monomials[i_mon];
    monomial_coeff mc = canonize_monomial(m);
    TRACE("nla_solver_rm", tout << "i_mon = " << i_mon << ", mon = ";
          print_monomial(m_monomials[i_mon], tout);
          tout << "\nmc = ";
          print_product(mc.vars(), tout);
          );
    m_rm_table.register_key_mono_in_rooted_monomials(mc, i_mon);        
}

template <typename T>
void core::trace_print_rms(const T& p, std::ostream& out) {
    out << "p = {";
    for (auto j : p) {
        out << "\nj = " << j <<
            ", rm = ";
        print_rooted_monomial(m_rm_table.rms()[j], out);
    }
    out << "\n}";
}

void core::print_monomial_stats(const monomial& m, std::ostream& out) {
    if (m.size() == 2) return;
    monomial_coeff mc = canonize_monomial(m);
    for(unsigned i = 0; i < mc.vars().size(); i++){
        if (abs(vvr(mc.vars()[i])) == rational(1)) {
            auto vv = mc.vars();
            vv.erase(vv.begin()+i);
            auto it = m_rm_table.vars_key_to_rm_index().find(vv);
            if (it == m_rm_table.vars_key_to_rm_index().end()) {
                out << "nf length" << vv.size() << "\n"; ;
            }
        }
    }
}
    
void core::print_stats(std::ostream& out) {
    // for (const auto& m : m_monomials) 
    //     print_monomial_stats(m, out);
    m_rm_table.print_stats(out);
}
        
void core::register_monomials_in_tables() {
    for (unsigned i = 0; i < m_monomials.size(); i++) 
        register_monomial_in_tables(i);

    m_rm_table.fill_rooted_monomials_containing_var();
    m_rm_table.fill_proper_multiples();
    TRACE("nla_solver_rm", print_stats(tout););
}

void core::clear() {
    m_var_to_its_monomial.clear();
    m_rm_table.clear();
    m_monomials_containing_var.clear();
    m_lemma_vec->clear();
}
    
void core::init_search() {
    clear();
    map_vars_to_monomials();
    init_vars_equivalence();
    register_monomials_in_tables();
}

#if 0
void init_to_refine() {
    m_to_refine.clear();
    for (auto const & m : m_emons) 
        if (!check_monomial(m)) 
            m_to_refine.push_back(m.var());
    
    TRACE("nla_solver", 
          tout << m_to_refine.size() << " mons to refine:\n");
    for (unsigned v : m_to_refine) tout << m_emons.var2monomial(v) << "\n";
}

std::unordered_set<lpvar> collect_vars(  const lemma& l) {
    std::unordered_set<lpvar> vars;
    auto insert_j = [&](lpvar j) { 
                        vars.insert(j);
                        auto const* m = m_emons.var2monomial(j);
                        if (m) for (lpvar k : *m) vars.insert(k);
                    };
    
    for (const auto& i : current_lemma().ineqs()) {
        for (const auto& p : i.term()) {                
            insert_j(p.var());
        }
    }
    for (const auto& p : current_expl()) {
        const auto& c = m_lar_solver.get_constraint(p.second);
        for (const auto& r : c.coeffs()) {
            insert_j(r.second);
        }
    }
    return vars;
}
#endif

void core::init_to_refine() {
    m_to_refine.clear();
    for (unsigned i = 0; i < m_monomials.size(); i++) {
        if (!check_monomial(m_monomials[i]))
            m_to_refine.push_back(i);
    }
    TRACE("nla_solver",
          tout << m_to_refine.size() << " mons to refine:\n";
          for (unsigned i: m_to_refine) {
              print_monomial_with_vars(m_monomials[i], tout);
          }
          );
}

bool core:: divide(const rooted_mon& bc, const factor& c, factor & b) const {
    svector<lpvar> c_vars = sorted_vars(c);
    TRACE("nla_solver_div",
          tout << "c_vars = ";
          print_product(c_vars, tout);
          tout << "\nbc_vars = ";
          print_product(bc.vars(), tout););
    if (!lp::is_proper_factor(c_vars, bc.vars()))
        return false;
            
    auto b_vars = lp::vector_div(bc.vars(), c_vars);
    TRACE("nla_solver_div", tout << "b_vars = "; print_product(b_vars, tout););
    SASSERT(b_vars.size() > 0);
    if (b_vars.size() == 1) {
        b = factor(b_vars[0]);
        return true;
    }
    auto it = m_rm_table.vars_key_to_rm_index().find(b_vars);
    if (it == m_rm_table.vars_key_to_rm_index().end()) {
        TRACE("nla_solver_div", tout << "not in rooted";);
        return false;
    }
    b = factor(it->second, factor_type::RM);
    TRACE("nla_solver_div", tout << "success div:"; print_factor(b, tout););
    return true;
}

void core::negate_factor_equality(const factor& c,
                                  const factor& d) {
    if (c == d)
        return;
    lpvar i = var(c);
    lpvar j = var(d);
    auto iv = vvr(i), jv = vvr(j);
    SASSERT(abs(iv) == abs(jv));
    if (iv == jv) {
        mk_ineq(i, -rational(1), j, llc::NE);
    } else { // iv == -jv
        mk_ineq(i, j, llc::NE, current_lemma());                
    }
}
    
void core::negate_factor_relation(const rational& a_sign, const factor& a, const rational& b_sign, const factor& b) {
    rational a_fs = canonize_sign(a);
    rational b_fs = canonize_sign(b);
    llc cmp = a_sign*vvr(a) < b_sign*vvr(b)? llc::GE : llc::LE;
    mk_ineq(a_fs*a_sign, var(a), - b_fs*b_sign, var(b), cmp);
}

void core::negate_var_factor_relation(const rational& a_sign, lpvar a, const rational& b_sign, const factor& b) {
    rational b_fs = canonize_sign(b);
    llc cmp = a_sign*vvr(a) < b_sign*vvr(b)? llc::GE : llc::LE;
    mk_ineq(a_sign, a, - b_fs*b_sign, var(b), cmp);
}

// |c_sign| = 1, and c*c_sign > 0
// ac > bc => ac/|c| > bc/|c| => a*c_sign > b*c_sign
void core::generate_ol(const rooted_mon& ac,                     
                       const factor& a,
                       int c_sign,
                       const factor& c,
                       const rooted_mon& bc,
                       const factor& b,
                       llc ab_cmp) {
    add_empty_lemma();
    rational rc_sign = rational(c_sign);
    mk_ineq(rc_sign * canonize_sign(c), var(c), llc::LE);
    mk_ineq(canonize_sign(ac), var(ac), -canonize_sign(bc), var(bc), ab_cmp);
    mk_ineq(canonize_sign(a)*rc_sign, var(a), - canonize_sign(b)*rc_sign, var(b), negate(ab_cmp));
    explain(ac, current_expl());
    explain(a, current_expl());
    explain(bc, current_expl());
    explain(b, current_expl());
    explain(c, current_expl());
    TRACE("nla_solver", print_lemma(tout););
}

void core::generate_mon_ol(const monomial& ac,                     
                           lpvar a,
                           const rational& c_sign,
                           lpvar c,
                           const rooted_mon& bd,
                           const factor& b,
                           const rational& d_sign,
                           lpvar d,
                           llc ab_cmp) {
    add_empty_lemma();
    mk_ineq(c_sign, c, llc::LE);
    explain(c, current_expl()); // this explains c == +- d
    negate_var_factor_relation(c_sign, a, d_sign, b);
    mk_ineq(ac.var(), -canonize_sign(bd), var(bd), ab_cmp);
    explain(bd, current_expl());
    explain(b, current_expl());
    explain(d, current_expl());
    TRACE("nla_solver", print_lemma(tout););
}

std::unordered_set<lpvar> core::collect_vars(const lemma& l) const {
    std::unordered_set<lpvar> vars;
    for (const auto& i : current_lemma().ineqs()) {
        for (const auto& p : i.term()) {
            lpvar j = p.var();
            vars.insert(j);
            auto it = m_var_to_its_monomial.find(j);
            if (it != m_var_to_its_monomial.end()) {
                for (lpvar k : m_monomials[it->second])
                    vars.insert(k);
            }
        }
    }
    for (const auto& p : current_expl()) {
        const auto& c = m_lar_solver.get_constraint(p.second);
        for (const auto& r : c.coeffs()) {
            lpvar j = r.second;
            vars.insert(j);
            auto it = m_var_to_its_monomial.find(j);
            if (it != m_var_to_its_monomial.end()) {
                for (lpvar k : m_monomials[it->second])
                    vars.insert(k);
            }
        }
    }
    return vars;
}

void core::print_lemma(std::ostream& out) {
    print_specific_lemma(current_lemma(), out);
}

void core::print_specific_lemma(const lemma& l, std::ostream& out) const {
    static int n = 0;
    out << "lemma:" << ++n << " ";
    print_ineqs(l, out);
    print_explanation(l.expl(), out);
    std::unordered_set<lpvar> vars = collect_vars(current_lemma());
        
    for (lpvar j : vars) {
        print_var(j, out);
    }
}
    

void core::trace_print_ol(const rooted_mon& ac,
                          const factor& a,
                          const factor& c,
                          const rooted_mon& bc,
                          const factor& b,
                          std::ostream& out) {
    out << "ac = ";
    print_rooted_monomial_with_vars(ac, out);
    out << "\nbc = ";
    print_rooted_monomial_with_vars(bc, out);
    out << "\na = ";
    print_factor_with_vars(a, out);
    out << ", \nb = ";
    print_factor_with_vars(b, out);
    out << "\nc = ";
    print_factor_with_vars(c, out);
}
    
bool core:: order_lemma_on_ac_and_bc_and_factors(const rooted_mon& ac,
                                                 const factor& a,
                                                 const factor& c,
                                                 const rooted_mon& bc,
                                                 const factor& b) {
    auto cv = vvr(c); 
    int c_sign = nla::rat_sign(cv);
    SASSERT(c_sign != 0);
    auto av_c_s = vvr(a)*rational(c_sign);
    auto bv_c_s = vvr(b)*rational(c_sign);        
    auto acv = vvr(ac); 
    auto bcv = vvr(bc); 
    TRACE("nla_solver", trace_print_ol(ac, a, c, bc, b, tout););
    // Suppose ac >= bc, then ac/|c| >= bc/|c|.
    // Notice that ac/|c| = a*c_sign , and bc/|c| = b*c_sign, which are correspondingly av_c_s and bv_c_s
    if (acv >= bcv && av_c_s < bv_c_s) {
        generate_ol(ac, a, c_sign, c, bc, b, llc::LT);
        return true;
    } else if (acv <= bcv && av_c_s > bv_c_s) {
        generate_ol(ac, a, c_sign, c, bc, b, llc::GT);
        return true;
    }
    return false;
}
    
// a >< b && c > 0  => ac >< bc
// a >< b && c < 0  => ac <> bc
// ac[k] plays the role of c   
bool core:: order_lemma_on_ac_and_bc(const rooted_mon& rm_ac,
                                     const factorization& ac_f,
                                     unsigned k,
                                     const rooted_mon& rm_bd) {
    TRACE("nla_solver",   tout << "rm_ac = ";
          print_rooted_monomial(rm_ac, tout);
          tout << "\nrm_bd = ";
          print_rooted_monomial(rm_bd, tout);
          tout << "\nac_f[k] = ";
          print_factor_with_vars(ac_f[k], tout););
    factor b;
    if (!divide(rm_bd, ac_f[k], b)){
        return false;
    }

    return order_lemma_on_ac_and_bc_and_factors(rm_ac, ac_f[(k + 1) % 2], ac_f[k], rm_bd, b);
}
void core::maybe_add_a_factor(lpvar i,
                              const factor& c,
                              std::unordered_set<lpvar>& found_vars,
                              std::unordered_set<unsigned>& found_rm,
                              vector<factor> & r) const {
    SASSERT(abs(vvr(i)) == abs(vvr(c)));
    auto it = m_var_to_its_monomial.find(i);
    if (it == m_var_to_its_monomial.end()) {
        i = m_evars.find(i).var();
        if (try_insert(i, found_vars)) {
            r.push_back(factor(i, factor_type::VAR));
        }
    } else {
        SASSERT(m_monomials[it->second].var() == i && abs(vvr(m_monomials[it->second])) == abs(vvr(c)));
        const index_with_sign & i_s = m_rm_table.get_rooted_mon(it->second);
        unsigned rm_i = i_s.index();
        //                SASSERT(abs(vvr(m_rm_table.rms()[i])) == abs(vvr(c)));
        if (try_insert(rm_i, found_rm)) {
            r.push_back(factor(rm_i, factor_type::RM));
            TRACE("nla_solver", tout << "inserting factor = "; print_factor_with_vars(factor(rm_i, factor_type::RM), tout); );
        }
    }
}
    
bool core:: order_lemma_on_ac_explore(const rooted_mon& rm, const factorization& ac, unsigned k) {
    const factor c = ac[k];
    TRACE("nla_solver", tout << "c = "; print_factor_with_vars(c, tout); );
    if (c.is_var()) {
        TRACE("nla_solver", tout << "var(c) = " << var(c););
        for (unsigned rm_bc : m_rm_table.var_map()[c.index()]) {
            TRACE("nla_solver", );
            if (order_lemma_on_ac_and_bc(rm ,ac, k, m_rm_table.rms()[rm_bc])) {
                return true;
            }
        }
    } else {
        for (unsigned rm_bc : m_rm_table.proper_multiples()[c.index()]) {
            if (order_lemma_on_ac_and_bc(rm , ac, k, m_rm_table.rms()[rm_bc])) {
                return true;
            }
        }
    }
    return false;
}

void core::order_lemma_on_factorization(const rooted_mon& rm, const factorization& ab) {
    const monomial& m = m_monomials[rm.orig_index()];
    TRACE("nla_solver", tout << "orig_sign = " << rm.orig_sign() << "\n";);
    rational sign = rm.orig_sign();
    for(factor f: ab)
        sign *= canonize_sign(f);
    const rational & fv = vvr(ab[0]) * vvr(ab[1]);
    const rational mv = sign * vvr(m);
    TRACE("nla_solver",
          tout << "ab.size()=" << ab.size() << "\n";
          tout << "we should have sign*vvr(m):" << mv << "=(" << sign << ")*(" << vvr(m) <<") to be equal to " << " vvr(ab[0])*vvr(ab[1]):" << fv << "\n";);
    if (mv == fv)
        return;
    bool gt = mv > fv;
    TRACE("nla_solver_f", tout << "m="; print_monomial_with_vars(m, tout); tout << "\nfactorization="; print_factorization(ab, tout););
    for (unsigned j = 0, k = 1; j < 2; j++, k--) {
        order_lemma_on_ab(m, sign, var(ab[k]), var(ab[j]), gt);
        explain(ab, current_expl()); explain(m, current_expl());
        explain(rm, current_expl());
        TRACE("nla_solver", print_lemma(tout););
        order_lemma_on_ac_explore(rm, ab, j);
    }
}
    
/**
   \brief Add lemma: 
   a > 0 & b <= value(b) => sign*ab <= value(b)*a  if value(a) > 0
   a < 0 & b >= value(b) => sign*ab <= value(b)*a  if value(a) < 0
*/
void core::order_lemma_on_ab_gt(const monomial& m, const rational& sign, lpvar a, lpvar b) {
    SASSERT(sign * vvr(m) > vvr(a) * vvr(b));
    add_empty_lemma();
    if (vvr(a).is_pos()) {
        TRACE("nla_solver", tout << "a is pos\n";);
        //negate a > 0
        mk_ineq(a, llc::LE);
        // negate b <= vvr(b)
        mk_ineq(b, llc::GT, vvr(b));
        // ab <= vvr(b)a
        mk_ineq(sign, m.var(), -vvr(b), a, llc::LE);
    } else {
        TRACE("nla_solver", tout << "a is neg\n";);
        SASSERT(vvr(a).is_neg());
        //negate a < 0
        mk_ineq(a, llc::GE);
        // negate b >= vvr(b)
        mk_ineq(b, llc::LT, vvr(b));
        // ab <= vvr(b)a
        mk_ineq(sign, m.var(), -vvr(b), a, llc::LE);
    }
}
// we need to deduce ab >= vvr(b)*a
/**
   \brief Add lemma: 
   a > 0 & b >= value(b) => sign*ab >= value(b)*a if value(a) > 0
   a < 0 & b <= value(b) => sign*ab >= value(b)*a if value(a) < 0
*/
void core::order_lemma_on_ab_lt(const monomial& m, const rational& sign, lpvar a, lpvar b) {
    SASSERT(sign * vvr(m) < vvr(a) * vvr(b));
    add_empty_lemma();
    if (vvr(a).is_pos()) {
        //negate a > 0
        mk_ineq(a, llc::LE);
        // negate b >= vvr(b)
        mk_ineq(b, llc::LT, vvr(b));
        // ab <= vvr(b)a
        mk_ineq(sign, m.var(), -vvr(b), a, llc::GE);
    } else {
        SASSERT(vvr(a).is_neg());
        //negate a < 0
        mk_ineq(a, llc::GE);
        // negate b <= vvr(b)
        mk_ineq(b, llc::GT, vvr(b));
        // ab >= vvr(b)a
        mk_ineq(sign, m.var(), -vvr(b), a, llc::GE);
    }
}


void core::order_lemma_on_ab(const monomial& m, const rational& sign, lpvar a, lpvar b, bool gt) {
    if (gt)
        order_lemma_on_ab_gt(m, sign, a, b);
    else 
        order_lemma_on_ab_lt(m, sign, a, b);
}
 
// void core::order_lemma_on_ab(const monomial& m, const rational& sign, lpvar a, lpvar b, bool gt) {
//     add_empty_lemma();
//     if (gt) {
//         if (vvr(a).is_pos()) {
//             //negate a > 0
//             mk_ineq(a, llc::LE);
//             // negate b >= vvr(b)
//             mk_ineq(b, llc::LT, vvr(b));
//             // ab <= vvr(b)a
//             mk_ineq(sign, m.var(), -vvr(b), a, llc::LE);
//         } else {
//             SASSERT(vvr(a).is_neg());
//             //negate a < 0
//             mk_ineq(a, llc::GE);
//             // negate b <= vvr(b)
//             mk_ineq(b, llc::GT, vvr(b));
//             // ab < vvr(b)a
//             mk_ineq(sign, m.var(), -vvr(b), a, llc::LE);  }
//     }
// }

void core::order_lemma_on_factor_binomial_explore(const monomial& m, unsigned k) {
    SASSERT(m.size() == 2);
    lpvar c = m[k];
    lpvar d = m_evars.find(c).var();
    auto it = m_rm_table.var_map().find(d);
    SASSERT(it != m_rm_table.var_map().end());
    for (unsigned bd_i : it->second) {
        order_lemma_on_factor_binomial_rm(m, k, m_rm_table.rms()[bd_i]);
        if (done())
            break;
    }        
}

void core::order_lemma_on_factor_binomial_rm(const monomial& ac, unsigned k, const rooted_mon& rm_bd) {
    factor d(m_evars.find(ac[k]).var(), factor_type::VAR);
    factor b;
    if (!divide(rm_bd, d, b))
        return;
    order_lemma_on_binomial_ac_bd(ac, k, rm_bd, b, d.index());
}

void core::order_lemma_on_binomial_ac_bd(const monomial& ac, unsigned k, const rooted_mon& bd, const factor& b, lpvar d) {
    TRACE("nla_solver",  print_monomial(ac, tout << "ac=");
          print_rooted_monomial(bd, tout << "\nrm=");
          print_factor(b, tout << ", b="); print_var(d, tout << ", d=") << "\n";);
    int p = (k + 1) % 2;
    lpvar a = ac[p];
    lpvar c = ac[k];
    SASSERT(m_evars.find(c).var() == d);
    rational acv = vvr(ac);
    rational av = vvr(a);
    rational c_sign = rrat_sign(vvr(c));
    rational d_sign = rrat_sign(vvr(d));
    rational bdv = vvr(bd);
    rational bv = vvr(b);
    auto av_c_s = av*c_sign; auto bv_d_s = bv*d_sign;

    // suppose ac >= bd, then ac/|c| >= bd/|d|.
    // Notice that ac/|c| = a*c_sign , and bd/|d| = b*d_sign
    if (acv >= bdv && av_c_s < bv_d_s)
        generate_mon_ol(ac, a, c_sign, c, bd, b, d_sign, d, llc::LT);
    else if (acv <= bdv && av_c_s > bv_d_s)
        generate_mon_ol(ac, a, c_sign, c, bd, b, d_sign, d, llc::GT);
        
}
    
void core::order_lemma_on_binomial_k(const monomial& m, lpvar k, bool gt) {
    SASSERT(gt == (vvr(m) > vvr(m[0]) * vvr(m[1])));
    unsigned p = (k + 1) % 2;
    order_lemma_on_binomial_sign(m, m[k], m[p], gt? 1: -1);
}
// sign it the sign of vvr(m) - vvr(m[0]) * vvr(m[1])
// m = xy
// and val(m) != val(x)*val(y)
// y > 0 and x = a, then xy >= ay
void core::order_lemma_on_binomial_sign(const monomial& ac, lpvar x, lpvar y, int sign) {
    SASSERT(!mon_has_zero(ac));
    int sy = rat_sign(y);
    add_empty_lemma();
    mk_ineq(y, sy == 1? llc::LE : llc::GE); // negate sy
    mk_ineq(x, sy*sign == 1? llc::GT:llc::LT , vvr(x)); // assert x <= vvr(x) if x > 0
    mk_ineq(ac.var(), - vvr(x), y, sign == 1?llc::LE:llc::GE);
    TRACE("nla_solver", print_lemma(tout););
}

void core::order_lemma_on_binomial(const monomial& ac) {
    TRACE("nla_solver", print_monomial(ac, tout););
    SASSERT(!check_monomial(ac) && ac.size() == 2);
    const rational & mult_val = vvr(ac[0]) * vvr(ac[1]);
    const rational acv = vvr(ac);
    bool gt = acv > mult_val;
    for (unsigned k = 0; k < 2; k++) {
        order_lemma_on_binomial_k(ac, k, gt);
        order_lemma_on_factor_binomial_explore(ac, k);
    }
}
// a > b && c > 0 => ac > bc
void core::order_lemma_on_rmonomial(const rooted_mon& rm) {
    TRACE("nla_solver_details",
          tout << "rm = "; print_product(rm, tout);
          tout << ", orig = "; print_monomial(m_monomials[rm.orig_index()], tout);
          );
    for (auto ac : factorization_factory_imp(rm, *this)) {
        if (ac.size() != 2)
            continue;
        if (ac.is_mon())
            order_lemma_on_binomial(*ac.mon());
        else
            order_lemma_on_factorization(rm, ac);
        if (done())
            break;
    }
}
    
void core::order_lemma() {
    TRACE("nla_solver", );
    init_rm_to_refine();
    const auto& rm_ref = m_rm_table.to_refine();
    unsigned start = random() % rm_ref.size();
    unsigned i = start;
    do {
        const rooted_mon& rm = m_rm_table.rms()[rm_ref[i]];
        order_lemma_on_rmonomial(rm);
        if (++i == rm_ref.size()) {
            i = 0;
        }
    } while(i != start && !done());
}

std::vector<rational> core::get_sorted_key(const rooted_mon& rm) const {
    std::vector<rational> r;
    for (unsigned j : rm.vars()) {
        r.push_back(abs(vvr(j)));
    }
    std::sort(r.begin(), r.end());
    return r;
}

void core::print_monotone_array(const vector<std::pair<std::vector<rational>, unsigned>>& lex_sorted,
                                std::ostream& out) const {
    out << "Monotone array :\n";
    for (const auto & t : lex_sorted ){
        out << "(";
        print_vector(t.first, out);
        out << "), rm[" << t.second << "]" << std::endl;
    }
    out <<  "}";
}
    
// Returns rooted monomials by arity
std::unordered_map<unsigned, unsigned_vector> core::get_rm_by_arity() {
    std::unordered_map<unsigned, unsigned_vector> m;
    for (unsigned i = 0; i < m_rm_table.rms().size(); i++ ) {
        unsigned arity = m_rm_table.rms()[i].vars().size();
        auto it = m.find(arity);
        if (it == m.end()) {
            it = m.insert(it, std::make_pair(arity, unsigned_vector()));
        }
        it->second.push_back(i);
    }
    return m;
}

    
bool core::uniform_le(const std::vector<rational>& a,
                       const std::vector<rational>& b,
                       unsigned & strict_i) const {
    SASSERT(a.size() == b.size());
    strict_i = -1;
    bool z_b = false;
        
    for (unsigned i = 0; i < a.size(); i++) {
        if (a[i] > b[i]){
            return false;
        }
            
        SASSERT(!a[i].is_neg());
        if (a[i] < b[i]){
            strict_i = i;
        } else if (b[i].is_zero()) {
            z_b = true;
        }
    }
    if (z_b) {strict_i = -1;}
    return true;
}

vector<std::pair<rational, lpvar>> core::get_sorted_key_with_vars(const rooted_mon& a) const {
    vector<std::pair<rational, lpvar>> r;
    for (lpvar j : a.vars()) {
        r.push_back(std::make_pair(abs(vvr(j)), j));
    }
    std::sort(r.begin(), r.end(), [](const std::pair<rational, lpvar>& a,
                                     const std::pair<rational, lpvar>& b) {
                                      return
                                          a.first < b.first ||
                                                    (a.first == b.first &&
                                                     a.second < b.second);
                                  });
    return r;
} 

void core::negate_abs_a_le_abs_b(lpvar a, lpvar b, bool strict) {
    rational av = vvr(a);
    rational as = rational(nla::rat_sign(av));
    rational bv = vvr(b);
    rational bs = rational(nla::rat_sign(bv));
    TRACE("nla_solver", tout << "av = " << av << ", bv = " << bv << "\n";);
    SASSERT(as*av <= bs*bv);
    llc mod_s = strict? (llc::LE): (llc::LT);
    mk_ineq(as, a, mod_s); // |a| <= 0 || |a| < 0
    if (a != b) {
        mk_ineq(bs, b, mod_s); // |b| <= 0 || |b| < 0
        mk_ineq(as, a, -bs, b, llc::GT); // negate |aj| <= |bj|
    }
}

void core::negate_abs_a_lt_abs_b(lpvar a, lpvar b) {
    rational av = vvr(a);
    rational as = rational(nla::rat_sign(av));
    rational bv = vvr(b);
    rational bs = rational(nla::rat_sign(bv));
    TRACE("nla_solver", tout << "av = " << av << ", bv = " << bv << "\n";);
    SASSERT(as*av < bs*bv);
    mk_ineq(as, a, llc::LT); // |aj| < 0
    mk_ineq(bs, b, llc::LT); // |bj| < 0
    mk_ineq(as, a, -bs, b, llc::GE); // negate  |aj| < |bj|
}

// a < 0 & b < 0 => a < b

void core::assert_abs_val_a_le_abs_var_b(
    const rooted_mon& a,
    const rooted_mon& b,
    bool strict) {
    lpvar aj = var(a);
    lpvar bj = var(b);
    rational av = vvr(aj);
    rational as = rational(nla::rat_sign(av));
    rational bv = vvr(bj);
    rational bs = rational(nla::rat_sign(bv));
    //        TRACE("nla_solver", tout << "rmv = " << rmv << ", jv = " << jv << "\n";);
    mk_ineq(as, aj, llc::LT); // |aj| < 0
    mk_ineq(bs, bj, llc::LT); // |bj| < 0
    mk_ineq(as, aj, -bs, bj, strict? llc::LT : llc::LE); // |aj| < |bj|
}

// strict version
void core::generate_monl_strict(const rooted_mon& a,
                                const rooted_mon& b,
                                unsigned strict) {
    add_empty_lemma();
    auto akey = get_sorted_key_with_vars(a);
    auto bkey = get_sorted_key_with_vars(b);
    SASSERT(akey.size() == bkey.size());
    for (unsigned i = 0; i < akey.size(); i++) {
        if (i != strict) {
            negate_abs_a_le_abs_b(akey[i].second, bkey[i].second, true);
        } else {
            mk_ineq(b[i], llc::EQ);
            negate_abs_a_lt_abs_b(akey[i].second, bkey[i].second);
        }
    }
    assert_abs_val_a_le_abs_var_b(a, b, true);
    explain(a, current_expl());
    explain(b, current_expl());
    TRACE("nla_solver", print_lemma(tout););
}

// not a strict version
void core::generate_monl(const rooted_mon& a,
                         const rooted_mon& b) {
    TRACE("nla_solver", tout <<
          "a = "; print_rooted_monomial_with_vars(a, tout) << "\n:";
          tout << "b = "; print_rooted_monomial_with_vars(a, tout) << "\n:";);
    add_empty_lemma();
    auto akey = get_sorted_key_with_vars(a);
    auto bkey = get_sorted_key_with_vars(b);
    SASSERT(akey.size() == bkey.size());
    for (unsigned i = 0; i < akey.size(); i++) {
        negate_abs_a_le_abs_b(akey[i].second, bkey[i].second, false);
    }
    assert_abs_val_a_le_abs_var_b(a, b, false);
    explain(a, current_expl());
    explain(b, current_expl());
    TRACE("nla_solver", print_lemma(tout););
}
    
bool core:: monotonicity_lemma_on_lex_sorted_rm_upper(const vector<std::pair<std::vector<rational>, unsigned>>& lex_sorted, unsigned i, const rooted_mon& rm) {
    const rational v = abs(vvr(rm));
    const auto& key = lex_sorted[i].first;
    TRACE("nla_solver", tout << "rm = ";
          print_rooted_monomial_with_vars(rm, tout); tout << "i = " << i << std::endl;);
    for (unsigned k = i + 1; k < lex_sorted.size(); k++) {
        const auto& p = lex_sorted[k];
        const rooted_mon& rmk = m_rm_table.rms()[p.second];
        const rational vk = abs(vvr(rmk));
        TRACE("nla_solver", tout << "rmk = ";
              print_rooted_monomial_with_vars(rmk, tout);
              tout << "\n";
              tout << "vk = " << vk << std::endl;);
        if (vk > v) continue;
        unsigned strict;
        if (uniform_le(key, p.first, strict)) {
            if (static_cast<int>(strict) != -1 && !has_zero(key)) {
                generate_monl_strict(rm, rmk, strict);
                return true;
            } else {
                if (vk < v) {
                    generate_monl(rm, rmk);
                    return true;
                }
            }
        }
            
    }
    return false;
}

bool core:: monotonicity_lemma_on_lex_sorted_rm_lower(const vector<std::pair<std::vector<rational>, unsigned>>& lex_sorted, unsigned i, const rooted_mon& rm) {
    const rational v = abs(vvr(rm));
    const auto& key = lex_sorted[i].first;
    TRACE("nla_solver", tout << "rm = ";
          print_rooted_monomial_with_vars(rm, tout); tout << "i = " << i << std::endl;);
        
    for (unsigned k = i; k-- > 0;) {
        const auto& p = lex_sorted[k];
        const rooted_mon& rmk = m_rm_table.rms()[p.second];
        const rational vk = abs(vvr(rmk));
        TRACE("nla_solver", tout << "rmk = ";
              print_rooted_monomial_with_vars(rmk, tout);
              tout << "\n";
              tout << "vk = " << vk << std::endl;);
        if (vk < v) continue;
        unsigned strict;
        if (uniform_le(p.first, key, strict)) {
            TRACE("nla_solver", tout << "strict = " << strict << std::endl;);
            if (static_cast<int>(strict) != -1) {
                generate_monl_strict(rmk, rm, strict);
                return true;
            } else {
                SASSERT(key == p.first); 
                if (vk < v) {
                    generate_monl(rmk, rm);
                    return true;
                }
            }
        }
            
    }
    return false;
}

bool core:: monotonicity_lemma_on_lex_sorted_rm(const vector<std::pair<std::vector<rational>, unsigned>>& lex_sorted, unsigned i, const rooted_mon& rm) {
    return monotonicity_lemma_on_lex_sorted_rm_upper(lex_sorted, i, rm)
        || monotonicity_lemma_on_lex_sorted_rm_lower(lex_sorted, i, rm);
}

bool core:: rm_check(const rooted_mon& rm) const {
    return check_monomial(m_monomials[rm.orig_index()]);
}
    
bool core::monotonicity_lemma_on_lex_sorted(const vector<std::pair<std::vector<rational>, unsigned>>& lex_sorted) {
    for (unsigned i = 0; i < lex_sorted.size(); i++) {
        unsigned rmi = lex_sorted[i].second;
        const rooted_mon& rm = m_rm_table.rms()[rmi];
        TRACE("nla_solver", print_rooted_monomial(rm, tout); tout << "\n, rm_check = " << rm_check(rm); tout << std::endl;); 
        if ((!rm_check(rm)) && monotonicity_lemma_on_lex_sorted_rm(lex_sorted, i, rm))
            return true;
    }
    return false;
}
    
bool core:: monotonicity_lemma_on_rms_of_same_arity(const unsigned_vector& rms) { 
    vector<std::pair<std::vector<rational>, unsigned>> lex_sorted;
    for (unsigned i : rms) {
        lex_sorted.push_back(std::make_pair(get_sorted_key(m_rm_table.rms()[i]), i));
    }
    std::sort(lex_sorted.begin(), lex_sorted.end(),
              [](const std::pair<std::vector<rational>, unsigned> &a,
                 const std::pair<std::vector<rational>, unsigned> &b) {
                  return a.first < b.first;
              });
    TRACE("nla_solver", print_monotone_array(lex_sorted, tout););
    return monotonicity_lemma_on_lex_sorted(lex_sorted);
}
    
void core::monotonicity_lemma() {
    unsigned i = random()%m_rm_table.m_to_refine.size();
    unsigned ii = i;
    do {
        unsigned rm_i = m_rm_table.m_to_refine[i];
        monotonicity_lemma(m_rm_table.rms()[rm_i].orig_index());
        if (done()) return;
        i++;
        if (i == m_rm_table.m_to_refine.size())
            i = 0;
    } while (i != ii);
}

#if 0
void core::monotonicity_lemma() {
    auto const& vars = m_rm_table.m_to_refine 
        unsigned sz = vars.size();
    unsigned start = random();
    for (unsigned j = 0; !done() && j < sz; ++j) {
        unsigned i = (start + j) % sz;
        monotonicity_lemma(*m_emons.var2monomial(vars[i]));
    }
}

#endif

void core::monotonicity_lemma(unsigned i_mon) {
    const monomial & m = m_monomials[i_mon];
    SASSERT(!check_monomial(m));
    if (mon_has_zero(m))
        return;
    const rational prod_val = abs(product_value(m));
    const rational m_val = abs(vvr(m));
    if (m_val < prod_val)
        monotonicity_lemma_lt(m, prod_val);
    else if (m_val > prod_val)
        monotonicity_lemma_gt(m, prod_val);
}


/**
   \brief Add |v| ~ |bound|
   where ~ is <, <=, >, >=, 
   and bound = vvr(v)

   |v| > |bound| 
   <=> 
   (v < 0 or v > |bound|) & (v > 0 or -v > |bound|)        
   => Let s be the sign of vvr(v)
   (s*v < 0 or s*v > |bound|)         

   |v| < |bound|
   <=>
   v < |bound| & -v < |bound| 
   => Let s be the sign of vvr(v)
   s*v < |bound|

*/

void core::add_abs_bound(lpvar v, llc cmp) {
    add_abs_bound(v, cmp, vvr(v));
}

void core::add_abs_bound(lpvar v, llc cmp, rational const& bound) {
    SASSERT(!vvr(v).is_zero());
    lp::lar_term t;  // t = abs(v)
    t.add_coeff_var(rrat_sign(vvr(v)), v);

    switch (cmp) {
    case llc::GT:
    case llc::GE:  // negate abs(v) >= 0
        mk_ineq(t, llc::LT, rational(0));
        break;
    case llc::LT:
    case llc::LE:
        break;
    default:
        UNREACHABLE();
        break;
    }
    mk_ineq(t, cmp, abs(bound));
}

/** \brief enforce the inequality |m| <= product |m[i]| .
    by enforcing lemma:
    /\_i |m[i]| <= |vvr(m[i])| => |m| <= |product_i vvr(m[i])|
    <=>
    \/_i |m[i]| > |vvr(m[i])} or |m| <= |product_i vvr(m[i])|
*/

void core::monotonicity_lemma_gt(const monomial& m, const rational& prod_val) {
    add_empty_lemma();
    for (lpvar j : m) {
        add_abs_bound(j, llc::GT);
    }
    lpvar m_j = m.var();
    add_abs_bound(m_j, llc::LE, prod_val);
    TRACE("nla_solver", print_lemma(tout););
}
    
/** \brief enforce the inequality |m| >= product |m[i]| .

    /\_i |m[i]| >= |vvr(m[i])| => |m| >= |product_i vvr(m[i])|
    <=>
    \/_i |m[i]| < |vvr(m[i])} or |m| >= |product_i vvr(m[i])|
*/
void core::monotonicity_lemma_lt(const monomial& m, const rational& prod_val) {
    add_empty_lemma();
    for (lpvar j : m) {
        add_abs_bound(j, llc::LT);
    }
    lpvar m_j = m.var();
    add_abs_bound(m_j, llc::GE, prod_val);
    TRACE("nla_solver", print_lemma(tout););
}
    
bool core:: find_bfc_to_refine_on_rmonomial(const rooted_mon& rm, bfc & bf) {
    for (auto factorization : factorization_factory_imp(rm, *this)) {
        if (factorization.size() == 2) {
            auto a = factorization[0];
            auto b = factorization[1];
            if (vvr(rm) != vvr(a) * vvr(b)) {
                bf = bfc(a, b);
                return true;
            }
        }
    }
    return false;
}
    
bool core:: find_bfc_to_refine(bfc& bf, lpvar &j, rational& sign, const rooted_mon*& rm_found){
    for (unsigned i: m_rm_table.to_refine()) {
        const auto& rm = m_rm_table.rms()[i]; 
        SASSERT (!check_monomial(m_monomials[rm.orig_index()]));
        if (rm.size() == 2) {
            sign = rational(1);
            const monomial & m = m_monomials[rm.orig_index()];
            j = m.var();
            rm_found = nullptr;
            bf.m_x = factor(m[0]);
            bf.m_y = factor(m[1]);
            return true;
        }
                
        rm_found = &rm;
        if (find_bfc_to_refine_on_rmonomial(rm, bf)) {
            j = m_monomials[rm.orig_index()].var();
            sign = rm.orig_sign();
            TRACE("nla_solver", tout << "found bf";
                  tout << ":rm:"; print_rooted_monomial(rm, tout) << "\n";
                  tout << "bf:"; print_bfc(bf, tout);
                  tout << ", product = " << vvr(rm) << ", but should be =" << vvr(bf.m_x)*vvr(bf.m_y);
                  tout << ", j == "; print_var(j, tout) << "\n";);
            return true;
        } 
    }
    return false;
}

void core::generate_simple_sign_lemma(const rational& sign, const monomial& m) {
    SASSERT(sign == nla::rat_sign(product_value(m)));
    for (lpvar j : m) {
        if (vvr(j).is_pos()) {
            mk_ineq(j, llc::LE);
        } else {
            SASSERT(vvr(j).is_neg());
            mk_ineq(j, llc::GE);
        }
    }
    mk_ineq(m.var(), (sign.is_pos()? llc::GT : llc ::LT));
    TRACE("nla_solver", print_lemma(tout););
}

void core::generate_simple_tangent_lemma(const rooted_mon* rm) {
    if (rm->size() != 2)
        return;
    TRACE("nla_solver", tout << "rm:"; print_rooted_monomial_with_vars(*rm, tout) << std::endl;);
    add_empty_lemma();
    unsigned i_mon = rm->orig_index();
    const monomial & m = m_monomials[i_mon];
    const rational v = product_value(m);
    const rational& mv = vvr(m);
    SASSERT(mv != v);
    SASSERT(!mv.is_zero() && !v.is_zero());
    rational sign = rational(nla::rat_sign(mv));
    if (sign != nla::rat_sign(v)) {
        generate_simple_sign_lemma(-sign, m);
        return;
    }
    bool gt = abs(mv) > abs(v);
    if (gt) {
        for (lpvar j : m) {
            const rational & jv = vvr(j);
            rational js = rational(nla::rat_sign(jv));
            mk_ineq(js, j, llc::LT);
            mk_ineq(js, j, llc::GT, jv);
        }
        mk_ineq(sign, i_mon, llc::LT);
        mk_ineq(sign, i_mon, llc::LE, v);
    } else {
        for (lpvar j : m) {
            const rational & jv = vvr(j);
            rational js = rational(nla::rat_sign(jv));
            mk_ineq(js, j, llc::LT);
            mk_ineq(js, j, llc::LT, jv);
        }
        mk_ineq(sign, m.var(), llc::LT);
        mk_ineq(sign, m.var(), llc::GE, v);
    }
    TRACE("nla_solver", print_lemma(tout););
}
    
void core::tangent_lemma() {
    bfc bf;
    lpvar j;
    rational sign;
    const rooted_mon* rm = nullptr;
        
    if (find_bfc_to_refine(bf, j, sign, rm)) {
        tangent_lemma_bf(bf, j, sign, rm);
    } else {
        TRACE("nla_solver", tout << "cannot find a bfc to refine\n"; );
        if (rm != nullptr)
            generate_simple_tangent_lemma(rm);
    }
}

void core::generate_explanations_of_tang_lemma(const rooted_mon& rm, const bfc& bf, lp::explanation& exp) {
    // here we repeat the same explanation for each lemma
    explain(rm, exp);
    explain(bf.m_x, exp);
    explain(bf.m_y, exp);
}
    
void core::tangent_lemma_bf(const bfc& bf, lpvar j, const rational& sign, const rooted_mon* rm){
    point a, b;
    point xy (vvr(bf.m_x), vvr(bf.m_y));
    rational correct_mult_val =  xy.x * xy.y;
    rational val = vvr(j) * sign;
    bool below = val < correct_mult_val;
    TRACE("nla_solver", tout << "rm = " << rm << ", below = " << below << std::endl; );
    get_tang_points(a, b, below, val, xy);
    TRACE("nla_solver", tout << "sign = " << sign << ", tang domain = "; print_tangent_domain(a, b, tout); tout << std::endl;);
    unsigned lemmas_size_was = m_lemma_vec->size();
    generate_two_tang_lines(bf, xy, sign, j);
    generate_tang_plane(a.x, a.y, bf.m_x, bf.m_y, below, j, sign);
    generate_tang_plane(b.x, b.y, bf.m_x, bf.m_y, below, j, sign);
    // if rm == nullptr there is no need to explain equivs since we work on a monomial and not on a rooted monomial
    if (rm != nullptr) { 
        lp::explanation expl;
        generate_explanations_of_tang_lemma(*rm, bf, expl);
        for (unsigned i = lemmas_size_was; i < m_lemma_vec->size(); i++) {
            auto &l = (*m_lemma_vec)[i];
            l.expl().add(expl);
        }
    }
    TRACE("nla_solver",
          for (unsigned i = lemmas_size_was; i < m_lemma_vec->size(); i++) 
              print_specific_lemma((*m_lemma_vec)[i], tout); );
}

void core::add_empty_lemma() {
    m_lemma_vec->push_back(lemma());
}
    
void core::generate_tang_plane(const rational & a, const rational& b, const factor& x, const factor& y, bool below, lpvar j, const rational& j_sign) {
    lpvar jx = var(x);
    lpvar jy = var(y);
    add_empty_lemma();
    negate_relation(jx, a);
    negate_relation(jy, b);
    bool sbelow = j_sign.is_pos()? below: !below;
#if Z3DEBUG
    int mult_sign = nla::rat_sign(a - vvr(jx))*nla::rat_sign(b - vvr(jy));
    SASSERT((mult_sign == 1) == sbelow);
    // If "mult_sign is 1"  then (a - x)(b-y) > 0 and ab - bx - ay + xy > 0
    // or -ab + bx + ay < xy or -ay - bx + xy > -ab
    // j_sign*vvr(j) stands for xy. So, finally we have  -ay - bx + j_sign*j > - ab
#endif

    lp::lar_term t;
    t.add_coeff_var(-a, jy);
    t.add_coeff_var(-b, jx);
    t.add_coeff_var( j_sign, j);
    mk_ineq(t, sbelow? llc::GT : llc::LT, - a*b);
}  

void core::negate_relation(unsigned j, const rational& a) {
    SASSERT(vvr(j) != a);
    if (vvr(j) < a) {
        mk_ineq(j, llc::GE, a);
    }
    else {
        mk_ineq(j, llc::LE, a);
    }
}
    
void core::generate_two_tang_lines(const bfc & bf, const point& xy, const rational& sign, lpvar j) {
    add_empty_lemma();
    mk_ineq(var(bf.m_x), llc::NE, xy.x);
    mk_ineq(sign, j, - xy.x, var(bf.m_y), llc::EQ);
        
    add_empty_lemma();
    mk_ineq(var(bf.m_y), llc::NE, xy.y);
    mk_ineq(sign, j, - xy.y, var(bf.m_x), llc::EQ);
        
}
// Get two planes tangent to surface z = xy, one at point a,  and another at point b.
// One can show that these planes still create a cut.
void core::get_initial_tang_points(point &a, point &b, const point& xy,
                                   bool below) const {
    const rational& x = xy.x;
    const rational& y = xy.y;
    if (!below){
        a = point(x - rational(1), y + rational(1));
        b = point(x + rational(1), y - rational(1));
    }
    else {
        a = point(x - rational(1), y - rational(1));
        b = point(x + rational(1), y + rational(1));
    }
}

void core::push_tang_point(point &a, const point& xy, bool below, const rational& correct_val, const rational& val) const {
    SASSERT(correct_val ==  xy.x * xy.y);
    int steps = 10;
    point del = a - xy;
    while (steps--) {
        del *= rational(2);
        point na = xy + del;
        TRACE("nla_solver", tout << "del = "; print_point(del, tout); tout << std::endl;);
        if (!plane_is_correct_cut(na, xy, correct_val, val, below)) {
            TRACE("nla_solver_tp", tout << "exit";tout << std::endl;);
            return;
        }
        a = na;
    }
}
    
void core::push_tang_points(point &a, point &b, const point& xy, bool below, const rational& correct_val, const rational& val) const {
    push_tang_point(a, xy, below, correct_val, val);
    push_tang_point(b, xy, below, correct_val, val);
}

rational core::tang_plane(const point& a, const point& x) const {
    return  a.x * x.y + a.y * x.x - a.x * a.y;
}

bool core:: plane_is_correct_cut(const point& plane,
                                 const point& xy,
                                 const rational & correct_val,                             
                                 const rational & val,
                                 bool below) const {
    SASSERT(correct_val ==  xy.x * xy.y);
    if (below && val > correct_val) return false;
    rational sign = below? rational(1) : rational(-1);
    rational px = tang_plane(plane, xy);
    return ((correct_val - px)*sign).is_pos() && !((px - val)*sign).is_neg();
        
}

// "below" means that the val is below the surface xy 
void core::get_tang_points(point &a, point &b, bool below, const rational& val,
                           const point& xy) const {
    get_initial_tang_points(a, b, xy, below);
    auto correct_val = xy.x * xy.y;
    TRACE("nla_solver", tout << "xy = "; print_point(xy, tout); tout << ", correct val = " << xy.x * xy.y;
          tout << "\ntang points:"; print_tangent_domain(a, b, tout);tout << std::endl;);
    TRACE("nla_solver", tout << "tang_plane(a, xy) = " << tang_plane(a, xy) << " , val = " << val;
          tout << "\ntang_plane(b, xy) = " << tang_plane(b, xy); tout << std::endl;);
    SASSERT(plane_is_correct_cut(a, xy, correct_val, val, below));
    SASSERT(plane_is_correct_cut(b, xy, correct_val, val, below));
    push_tang_points(a, b, xy, below, correct_val, val);
    TRACE("nla_solver", tout << "pushed a = "; print_point(a, tout); tout << "\npushed b = "; print_point(b, tout); tout << std::endl;);
}

bool core:: conflict_found() const {
    for (const auto & l : * m_lemma_vec) {
        if (l.is_conflict())
            return true;
    }
    return false;
}

bool core:: done() const {
    return m_lemma_vec->size() >= 10 || conflict_found();
}
        
lbool core:: inner_check(bool derived) {
    for (int search_level = 0; search_level < 3 && !done(); search_level++) {
        TRACE("nla_solver", tout << "derived = " << derived << ", search_level = " << search_level << "\n";);
        if (search_level == 0) {
            m_basics.basic_lemma(derived);
            if (!m_lemma_vec->empty())
                return l_false;
        }
        if (derived) continue;
        TRACE("nla_solver", tout << "passed derived and basic lemmas\n";);
        if (search_level == 1) {
            order_lemma();
        } else { // search_level == 2
            monotonicity_lemma();
            tangent_lemma();
        }
    }
    return m_lemma_vec->empty()? l_undef : l_false;
}
    
lbool core:: check(vector<lemma>& l_vec) {
    settings().st().m_nla_calls++;
    TRACE("nla_solver", tout << "calls = " << settings().st().m_nla_calls << "\n";);
    m_lemma_vec =  &l_vec;
    if (!(m_lar_solver.get_status() == lp::lp_status::OPTIMAL || m_lar_solver.get_status() == lp::lp_status::FEASIBLE )) {
        TRACE("nla_solver", tout << "unknown because of the m_lar_solver.m_status = " << lp_status_to_string(m_lar_solver.get_status()) << "\n";);
        return l_undef;
    }

    init_to_refine();
    if (m_to_refine.empty()) {
        return l_true;
    }
    init_search();
    lbool ret = inner_check(true);
    if (ret == l_undef)
        ret = inner_check(false);

    TRACE("nla_solver", tout << "ret = " << ret << ", lemmas count = " << m_lemma_vec->size() << "\n";);
    IF_VERBOSE(2, if(ret == l_undef) {verbose_stream() << "Monomials\n"; print_monomials(verbose_stream());});
    CTRACE("nla_solver", ret == l_undef, tout << "Monomials\n"; print_monomials(tout););
    return ret;
}

bool core:: no_lemmas_hold() const {
    for (auto & l : * m_lemma_vec) {
        if (lemma_holds(l)) {
            TRACE("nla_solver", print_specific_lemma(l, tout););
            return false;
        }
    }
    return true;
}
    
void core::test_factorization(unsigned /*mon_index*/, unsigned /*number_of_factorizations*/) {
    //  vector<ineq> lemma;

    // unsigned_vector vars = m_monomials[mon_index].vars();
        
    // factorization_factory_imp fc(vars, // 0 is the index of "abcde"
    //                              *this);
     
    // std::cout << "factorizations = of "; print_monomial(m_monomials[mon_index], std::cout) << "\n";
    // unsigned found_factorizations = 0;
    // for (auto f : fc) {
    //     if (f.is_empty()) continue;
    //     found_factorizations ++;
    //     print_factorization(f, std::cout);
    //     std::cout << std::endl;
    // }
    // SASSERT(found_factorizations == number_of_factorizations);
}
    
lbool core:: test_check(
    vector<lemma>& l) {
    m_lar_solver.set_status(lp::lp_status::OPTIMAL);
    return check(l);
}
template rational core::product_value<monomial>(const monomial & m) const;

} // end of nla