#include "calculator/math/calculator_engine.h"
#include "calculator/assets/calculator_constants.h"
#include "calculator/math/calculator_conversions.h"
#include "calculator/math/calculator_eng_symbols.h"
#include "calculator/math/calculator_loose_paren.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numeric>

namespace Calculator {
namespace {
constexpr int64_t kExactLimit = 1000000000000000LL;  // ClassWiz display terms (Value::exact)
ExactIntegral exactIntegralHook = nullptr;  // installed by the CAS (setExactIntegral)
Exact rational(int64_t n, int64_t d=1, int64_t rad=1, bool pi=false) {
  Exact e;
  if (!d || n < -kExactLimit || n > kExactLimit || d < -kExactLimit || d > kExactLimit || rad < 1) return e;
  if (d<0) { d=-d; n=-n; }
  const auto g=std::gcd(n,d);
  e.numerator=n/g; e.denominator=d/g; e.radicand=rad; e.pi=pi; e.valid=true;
  if (!n) { e.radicand=1; e.pi=false; }
  return e;
}
bool isRelation(const Expression& n) {
  return n && n->kind==Node::Kind::Binary && (n->text=="="||n->text=="=="||n->text=="!="||n->text=="<"||n->text==">"||n->text=="<="||n->text==">=");
}
// An operation whose exact result is `exact` (anything but Inexact) and whose
// decimal twin, computed from the operands' decimals, is `fromOps`. The twin
// is kept -- it costs nothing and loses nothing -- except where the exact form
// gives the decimal cheaply and exactly (rationals), or where the twin lost
// its leading digits to cancellation. An exact failure is NaN, so the
// evaluator's range check reports Math ERROR.
Value settle(const ExactNumber& exact, const Decimal& fromOps, bool cancelled=false) {
  Value v;
  v.symbolic=exact;
  if(exact.failed()) {v.number=Decimal::nan();return v;}
  if(exact.isRational() || cancelled || !fromOps.finite()) {
    const Decimal d=exact.toDecimal();
    v.number=d.finite()?d:fromOps;
  } else v.number=fromOps;
  return v;
}
Value combine(const Value& a, const Value& b, char op) {
  const Decimal d=op=='+'?a.number+b.number:op=='-'?a.number-b.number:op=='*'?a.number*b.number:a.number/b.number;
  if(a.symbolic.exact() && b.symbolic.exact()) {
    const ExactNumber s=op=='+'?a.symbolic+b.symbolic:op=='-'?a.symbolic-b.symbolic:
                        op=='*'?a.symbolic*b.symbolic:a.symbolic/b.symbolic;
    if(s.status()!=ExactStatus::Inexact) {
      // A sum twenty orders below its operands kept almost none of their digits.
      const bool cancelled=(op=='+'||op=='-') && d.abs()*Decimal("1E20")<std::max(a.number.abs(),b.number.abs());
      return settle(s,d,cancelled);
    }
  }
  return Value::approximate(d);
}
Value piValue() {return settle(ExactNumber::pi(),Decimal::pi());}
// The whole number of degrees of an exact angle: q*pi radians, or rational
// degrees/gradians. Recognized from the exact form, never from a decimal.
bool exactDegrees(const Value& angle, Angle unit, int64_t& degrees) {
  if(!angle.symbolic.exact()) return false;
  Rational q;
  if(unit==Angle::Radian) {if(!angle.symbolic.piMultiple(q))return false;q=q*Rational(180);}
  else if(angle.symbolic.isRational()) q=angle.symbolic.rationalValue()*(unit==Angle::Gradian?Rational::of(9,10):Rational(1));
  else return false;
  if(!q.integer()) return false;
  const BigInt turn=q.num()%BigInt(360);
  degrees=turn.toInt64();
  return true;
}
// sin (or cos) of a whole number of degrees that is a multiple of 30 or 45.
bool standardTrig(bool sine, int64_t degrees, ExactNumber& out) {
  if(degrees%30 && degrees%45) return false;
  int k=static_cast<int>((degrees%360+360)%360);
  if(!sine)k=(k+90)%360;
  const int sign=k>180?-1:1;int acute=k>180?360-k:k;if(acute>90)acute=180-acute;
  const ExactNumber s=ExactNumber::integer(sign);
  if(acute==0)out=ExactNumber::integer(0);
  else if(acute==90)out=s;
  else if(acute==30)out=s*ExactNumber::fraction(1,2);
  else if(acute==60)out=s*ExactNumber::fraction(1,2)*ExactNumber::integer(3).sqrt();
  else out=s*ExactNumber::fraction(1,2)*ExactNumber::integer(2).sqrt();
  return true;
}
// An exact number of degrees in `unit`: q*pi for radians.
ExactNumber exactAngle(int64_t degrees, Angle unit) {
  if(unit==Angle::Degree) return ExactNumber::integer(degrees);
  if(unit==Angle::Gradian) return ExactNumber::fraction(degrees*10,9);
  return ExactNumber::fraction(degrees,180)*ExactNumber::pi();
}
// asin/acos/atan of the exact values whose angle is a multiple of 30 or 45
// degrees, matched structurally against the canonical forms.
bool inverseTrigDegrees(const std::string& fn, const ExactNumber& x, int64_t& degrees) {
  if(!x.exact()) return false;
  const ExactNumber half=ExactNumber::fraction(1,2),r2=ExactNumber::integer(2).sqrt()*half,r3=ExactNumber::integer(3).sqrt();
  struct Known {ExactNumber value;int64_t degrees;};
  const bool tangent=fn=="atan";
  const Known table[]={
    {ExactNumber::integer(0),0},{tangent?r3*ExactNumber::fraction(1,3):half,30},{tangent?ExactNumber::integer(1):r2,45},
    {tangent?r3:r3*half,60},{tangent?ExactNumber():ExactNumber::integer(1),90}};
  for(const auto& k:table) {
    if(!k.value.exact()) continue;
    int64_t d=0;
    if(x==k.value) d=k.degrees;
    else if(!k.value.zero() && x==-k.value) d=-k.degrees;
    else continue;
    degrees=fn=="acos"?90-d:d;
    return true;
  }
  return false;
}
// The exact principal argument of (x, y) at a multiple of 30 or 45 degrees.
bool exactArgument(const Value& x, const Value& y, int64_t& degrees) {
  if(!x.symbolic.exact()||!y.symbolic.exact()) return false;
  bool certainX=false,certainY=false;
  const int sx=x.symbolic.sign(&certainX),sy=y.symbolic.sign(&certainY);
  if(!certainX||!certainY||(!sx&&!sy)) return false;
  int64_t acute=0;
  if(!sx) acute=90;
  else if(sy) {
    const ExactNumber ratio=(y.symbolic/x.symbolic).abs();
    if(!inverseTrigDegrees("atan",ratio,acute)) return false;
  }
  degrees=sx<0?180-acute:acute;
  if(sy<0) degrees=-degrees;
  return true;
}
class Parser {
 public:
  explicit Parser(const std::string& s):s_(s) { next(); }
  Parsed run() {
    if(s_.size()>2048) return {nullptr,Error::Stack,0};
    auto root=expression(0,0);
    if(error_==Error::None && !token_.empty()) fail();
    return {root,error_,errorPosition_};
  }
 private:
  const std::string& s_; size_t at_=0,pos_=0,count_=0,errorPosition_=0;
  std::string token_; Error error_=Error::None;
  void failAt(size_t p,Error e=Error::Syntax) {if(error_==Error::None){error_=e;errorPosition_=p;}}
  void fail(Error e=Error::Syntax) {failAt(pos_,e);}
  void next() {
    while(at_<s_.size() && std::isspace(static_cast<unsigned char>(s_[at_]))) ++at_;
    pos_=at_; token_.clear(); if(at_==s_.size()) return;
    const char c=s_[at_++]; token_+=c;
    if(std::isdigit(static_cast<unsigned char>(c)) || c=='.') {
      while(at_<s_.size() && (std::isdigit(static_cast<unsigned char>(s_[at_])) || s_[at_]=='.')) token_+=s_[at_++];
      if(at_<s_.size() && s_[at_]=='E' && at_+1<s_.size() &&
          (std::isdigit(static_cast<unsigned char>(s_[at_+1])) || s_[at_+1]=='-' || s_[at_+1]=='+')) {
        token_+=s_[at_++]; if(s_[at_]=='-' || s_[at_]=='+') token_+=s_[at_++];
        while(at_<s_.size() && std::isdigit(static_cast<unsigned char>(s_[at_]))) token_+=s_[at_++];
      }
    } else if(std::isalpha(static_cast<unsigned char>(c))) {
      // Upper-case variable letters and x/y/z are single keys. Names used by
      // catalog functions are lower-case, except Ans/PreAns.
      if((c>='A'&&c<='F' && s_.compare(pos_,3,"Ans")!=0) || c=='x'||c=='y'||c=='z') return;
      // Infix permutation/combination operators: a bare 'P' is always the
      // operator (no variable P exists), split from following digits so
      // "4P3" lexes as 4, P, 3. "PreAns" keeps its multi-char token.
      // 'C' is already a single key above; the parser decides operator vs
      // variable C by position (see expression()).
      if(c=='P' && s_.compare(pos_,6,"PreAns")!=0) return;
      while(at_<s_.size() && (std::isalnum(static_cast<unsigned char>(s_[at_])) || s_[at_]=='_')) token_+=s_[at_++];
    } else if(at_<s_.size() && s_[at_]=='=' && (c=='<'||c=='>'||c=='!'||c=='=')) token_+=s_[at_++];
  }
  Expression node(Node::Kind kind, std::string text, size_t p, std::vector<Expression> children={}) {
    if(++count_>512) {fail(Error::Stack);return nullptr;}
    return std::make_shared<Node>(Node{kind,std::move(text),p,std::move(children)});
  }
  bool looseOpen() const {return token_.size()==1 && token_[0]==LooseParen::kOpen;}
  // A loose '(' has no partner in its template field, so it closes where the
  // field does: at the field's ',' or ')' (left for the template) or the end.
  void closeLoose() {if(!token_.empty() && token_!="," && token_!=")") fail();}
  bool primary() const {
    return !token_.empty() && (std::isalnum(static_cast<unsigned char>(token_[0])) || token_[0]=='.'||token_=="("||looseOpen());
  }
  Expression expression(int min, unsigned depth) {
    if(depth>32 || error_!=Error::None) {fail(Error::Stack);return nullptr;}
    const auto t=token_; const auto p=pos_; next(); Expression a;
    if(t=="+"||t=="-") a=node(Node::Kind::Unary,t,p,{expression(35,depth+1)});
    else if(t=="(") {a=expression(0,depth+1); if(token_==")") next(); else if(!token_.empty()) fail();}
    else if(t.size()==1 && t[0]==LooseParen::kOpen) {a=expression(0,depth+1); closeLoose();}
    else if(!t.empty() && (std::isdigit(static_cast<unsigned char>(t[0])) || t[0]=='.')) a=node(Node::Kind::Number,t,p);
    else if(!t.empty() && std::isalpha(static_cast<unsigned char>(t[0]))) {
      const bool call=variableIndex(t)<0 && t!="pi" && t!="e" && t!="Ans" && t!="PreAns";
      if(call && looseOpen()) {
        // "sin" + loose '(' inside a template field: one argument, closed at
        // the field end (its commas belong to the template).
        next(); auto arg=expression(0,depth+1); closeLoose();
        a=node(Node::Kind::Call,t,p,{arg});
      } else if(token_=="(" && call) {
        next(); std::vector<Expression> args;
        if(token_!=")") {
          do {if(!args.empty())next();args.push_back(expression(0,depth+1));} while(token_=="," && error_==Error::None);
        }
        if(token_==")") next(); else if(!token_.empty()) fail();
        a=node(Node::Kind::Call,t,p,std::move(args));
      } else a=node(Node::Kind::Symbol,t,p);
    } else {failAt(p);return nullptr;} // Rejected primary, not the next lookahead token.
    while(error_==Error::None) {
      auto op=token_; bool implicit=false; int prec=0;
      // Engineering symbols are postfix x10^n, bound like % (5k^2 is (5k)^2).
      if(op=="!"||op=="%"||EngSymbol::is(op[0])) prec=50;
      else if(op=="^") prec=45;
      // Polar angle r@theta (Complex): tighter than * and /, looser than implicit
      // multiplication, so 2x@30 is (2x)@30 and 3*2@30 is 3*(2@30).
      else if(op=="@") prec=25;
      // Infix permutation/combination (4P3, 4C3): tighter than * and /,
      // looser than implicit multiplication, so 2*4P3 is 2*(4P3).
      // 'C' here is in operator position (after a complete operand); a 'C'
      // in primary position stays the variable C (see primary parsing).
      else if(op=="P"||op=="C") prec=27;
      // ' is the Vector app's dot product, at the level of * and /.
      else if(op=="*"||op=="/"||op=="'") prec=20;
      else if(op=="+"||op=="-") prec=10;
      else if(op=="="||op=="=="||op=="!="||op=="<"||op==">"||op=="<="||op==">=") prec=5;
      else if(primary()) {op="*";prec=30;implicit=true;}
      else break;
      if(prec<min) break;
      const size_t position=pos_;
      if(!implicit) next();
      if(op=="!"||op=="%"||EngSymbol::is(op[0])) {a=node(Node::Kind::Unary,op,position,{a});continue;}
      auto b=expression(prec+(op=="^"?0:1),depth+1);
      a=node(Node::Kind::Binary,op,position,{a,b});
    }
    return a;
  }
};
}
Value::Value(const Decimal& d):number(d) {
  int64_t n; if(d.toInt(n)) symbolic=ExactNumber::integer(n);
}
Value::Value(int n):number(n),symbolic(ExactNumber::integer(n)) {}
Value Value::of(const ExactNumber& exact) {return settle(exact,Decimal::nan());}
Value Value::approximate(const Decimal& d) {Value v;v.number=d;return v;}
void setExactIntegral(ExactIntegral integral) {exactIntegralHook=integral;}
namespace {
bool displayTerm(const ExactNumber::SimpleTerm& t, Exact& e) {
  const auto& c=t.coefficient;
  if(!c.num().fitsInt64()||!c.den().fitsInt64()||!t.radicand.fitsInt64()) return false;
  e=rational(c.num().toInt64(),c.den().toInt64(),t.radicand.toInt64(),t.pi);
  e.euler=t.e;
  return e.valid && e.radicand<=kExactLimit;
}
// At most two ClassWiz terms, or neither is shown exactly.
bool displayTerms(const Value& v, Exact& first, Exact& second) {
  std::vector<ExactNumber::SimpleTerm> terms;
  if(!v.symbolic.simpleTerms(terms) || terms.size()>2) return false;
  if(terms.empty()) {first=rational(0);return true;}
  if(!displayTerm(terms[0],first)) return false;
  if(terms.size()==2 && !displayTerm(terms[1],second)) {first=Exact{};return false;}
  return true;
}
}
Exact Value::exact() const {Exact a,b;displayTerms(*this,a,b);return a;}
Exact Value::extra() const {Exact a,b;return displayTerms(*this,a,b)?b:Exact{};}
// Tried only where the ordinary view fails. Read off the normal form
// (constantTerms), never computed: presentation runs on the loop task, whose
// 8 KB stack an exact division here overflowed (Stack canary, loopTask).
// One or two c / c*sqrt(n) terms after the constant; pi^2, pi*e keep the decimal.
FactoredExact Value::factored() const {
  FactoredExact f;
  std::vector<ExactNumber::SimpleTerm> terms;
  bool pi=false;
  if(!symbolic.constantTerms(pi,terms)||terms.size()>2)return f;
  if(!displayTerm(terms[0],f.first)||(terms.size()==2&&!displayTerm(terms[1],f.second)))return FactoredExact{};
  f.pi=pi;f.valid=true;
  return f;
}
int variableIndex(const std::string& n) {
  if(n.size()!=1) return -1;
  if(n[0]>='A'&&n[0]<='F') return n[0]-'A';
  if(n[0]>='x'&&n[0]<='z') return n[0]-'x'+6;
  return -1;
}
Parsed parse(const std::string& s) {return Parser(s).run();}
const char* errorText(Error e) {
  static const char* names[]={"","Syntax ERROR","Math ERROR","Argument ERROR","Stack ERROR",
    "Circular ERROR","Not Defined","No Operator","Time Out","Cancelled",
    "Dimension ERROR","Variable ERROR","Cannot Solve","Range ERROR","Memory ERROR"};
  return names[static_cast<unsigned>(e)];
}

namespace {
class Evaluator {
 public:
  Evaluator(const Request& request,const std::function<bool()>& stop,const std::function<void()>& yield)
    :q(request),cancel(stop),cooperate(yield),rng(request.randomSeed?request.randomSeed:1) {r.memory=q.memory;}
  Result run() {
    auto p=parse(q.expression); if(p.error!=Error::None){r.error=p.error;r.position=p.position;return r;}
    if(q.verify) {
      std::vector<Expression> operands; std::vector<std::string> relations;
      flattenRelations(p.root,operands,relations);
      r.error=verifyChainError(relations);
      if(r.error!=Error::None) return r;
      bool truth=true; Value left=eval(operands[0]);
      for(size_t i=0;i<relations.size() && r.error==Error::None;++i) {
        Value right=eval(operands[i+1]);const int c=compareValues(left,right);const auto& op=relations[i];
        truth=truth && (op=="="||op=="=="?c==0:op=="!="?c!=0:op=="<"?c<0:op==">"?c>0:op=="<="?c<=0:c>=0);
        left=right;
      }
      r.value=Value(truth?1:0);r.verified=true;r.truth=truth;
    } else r.value=eval(p.root);
    if(r.error==Error::None) {
      // PreAns is real: after the Complex app left a complex Ans it restarts at 0.
      r.memory.preAns=q.memory.ans.complex?Value(0):q.memory.ans.re;
      // Once per answer, the decimal of an irrational exact result comes from
      // the exact form itself, not from the chain of decimal operations.
      if(r.value.symbolic.exact() && !r.value.symbolic.isRational()) {
        const Decimal d=r.value.symbolic.toDecimal();if(d.finite())r.value.number=d;
      }
      r.value.number=r.value.number.rounded();r.memory.ans=r.value;
    } else r.memory=q.memory;
    return r;
  }
  Result runTree(const Expression& root) {
    r.value=eval(root);
    if(r.error!=Error::None) r.memory=q.memory;
    return r;
  }
  Result runOperation(const Expression& root, const std::vector<Value>& arguments) {
    if (!root || root->children.size() != arguments.size()) {
      r.error = Error::Argument;
      return r;
    }
    // Analysis commands bind and repeatedly evaluate an expression, rather
    // than apply a scalar function to values already computed by the caller.
    if (root->kind == Node::Kind::Call && (root->text == "sum" || root->text == "product" ||
        root->text == "integral" || root->text == "diff")) {
      r.error = Error::Syntax;
      r.position = root->position;
      return r;
    }
    operation = root.get();
    operands = &arguments;
    return runTree(root);
  }
 private:
  const Request& q; Result r; std::function<bool()> cancel;std::function<void()> cooperate;
  const Node* operation = nullptr;
  const std::vector<Value>* operands = nullptr;
  uint32_t rng,steps=0;unsigned depth=0;uint8_t functions=0;
  bool tick() {
    if(r.error!=Error::None) return false;
    if(cancel && cancel()){r.error=Error::Cancelled;return false;}
    if(++steps>2000000){r.error=Error::TimeOut;return false;}
    if((steps&31)==0 && cooperate) cooperate();
    return true;
  }
  Value fail(Error e,size_t p) {if(r.error==Error::None){r.error=e;r.position=p;}return Value();}
  static bool relation(const Expression& n) {return isRelation(n);}
  // A complex Ans or variable referenced in a real context: Math ERROR at the
  // reference. Complex values nobody references stay harmless.
  Value real(const ScalarValue& v,size_t p) {if(v.complex)return fail(Error::Math,p);return v.re;}
  Value checked(Value v,size_t p) {
    if(!v.number.finite() || v.number.abs()>Decimal("9.999999999E99")) return fail(Error::Math,p);
    if(!v.number.zero() && v.number.abs()<Decimal("1E-99")) return Value(0);
    return v;
  }
  Decimal rad(const Decimal& a) {return q.settings.angle==Angle::Degree?a*Decimal::pi()/180:q.settings.angle==Angle::Gradian?a*Decimal::pi()/200:a;}
  Decimal angle(const Decimal& a) {return q.settings.angle==Angle::Degree?a*180/Decimal::pi():q.settings.angle==Angle::Gradian?a*200/Decimal::pi():a;}
  Value factorial(const Value& a,size_t p) {
    int64_t n;if(!a.number.toInt(n)||n<0||n>69)return fail(Error::Math,p);
    Value v(1);for(int i=2;i<=n;++i)v=combine(v,Value(i),'*');return v;
  }
  Value eval(const Expression& n) {
    if(!n||!tick()) return Value();
    if (operation && n.get() != operation)
      for (size_t i = 0; i < operation->children.size(); ++i)
        if (n == operation->children[i]) return checked((*operands)[i], n->position);
    if(++depth>64){--depth;return fail(Error::Stack,n->position);}
    Value v=body(n);--depth;return checked(v,n->position);
  }
  Value body(const Expression& n) {
    const auto& t=n->text;const auto pos=n->position;
    if(n->kind==Node::Kind::Number) {
      // A typed literal is exactly the rational it spells, at any length.
      const ExactNumber literal=ExactNumber::fromLiteral(t);
      Value v=literal.exact()?settle(literal,Decimal(t)):Value::approximate(Decimal(t));
      if(!v.number.finite()) return fail(Error::Syntax,pos);
      return v;
    }
    if(n->kind==Node::Kind::Symbol) {
      int i=variableIndex(t);if(i>=0)return real(r.memory.variables[i],pos);
      if(t=="Ans")return real(q.memory.ans,pos);
      if(t=="PreAns")return q.memory.preAns;
      if(t=="pi")return piValue();
      if(t=="e")return settle(ExactNumber::e(),Decimal(1).exp());
      return fail(Error::Syntax,pos);
    }
    if(n->kind==Node::Kind::Unary) {
      Value v=eval(n->children[0]);
      if(t=="-")v=realNegate(v);
      if(t=="%")return combine(v,Value(100),'/');
      if(EngSymbol::is(t[0])){
        // Exact: 10^|n| is an integer (at most 1E18), so 5m stays 1/200.
        const int n=EngSymbol::exponent(t[0]);
        return combine(v,Value(Decimal(("1E"+std::to_string(n<0?-n:n)).c_str())),n<0?'/':'*');
      }
      if(t=="!")return factorial(v,pos);
      return v;
    }
    if(n->kind==Node::Kind::Binary) {
      if(relation(n)) return fail(Error::Syntax,pos);
      Value a=eval(n->children[0]),b=eval(n->children[1]);
      if(t=="+")return combine(a,b,'+');if(t=="-")return combine(a,b,'-');
      if(t=="*")return combine(a,b,'*');if(t=="/")return combine(a,b,'/');
      if(t=="^")return power(a,b,pos);
      if(t=="P")return perm(a,b,false,pos);
      if(t=="C")return perm(a,b,true,pos);
      return fail(Error::Syntax,pos);
    }
    if(t=="sum"||t=="product"||t=="integral"||t=="diff")return analysis(n);
    std::vector<Value> a;
    for(auto& child:n->children)a.push_back(eval(child));
    if(r.error!=Error::None)return Value();
    if(t=="rand" && a.empty()) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return combine(Value(static_cast<int>(rng%1000)),Value(1000),'/');}
    if(a.empty())return fail(Error::Argument,pos);
    const Decimal x=a[0].number;
    if(t=="constant") {
      int64_t index;
      if(a.size()!=1||!x.toInt(index)||index<0||index>=47)return fail(Error::Argument,pos);
      auto parsed=parse(kConstants[index].expression);
      return eval(parsed.root);
    }
    if(t=="convert") {
      int64_t index;
      if(a.size()!=2||!a[1].number.toInt(index)||index<0||index>=static_cast<int64_t>(sizeof(kConversions)/sizeof(kConversions[0])))return fail(Error::Argument,pos);
      const auto& entry=kConversions[index];
      return Value(entry.inverse?(x-Decimal(entry.offset))/Decimal(entry.factor):x*Decimal(entry.factor)+Decimal(entry.offset));
    }
    if(t=="f"||t=="g") {
      if(a.size()!=1)return fail(Error::Argument,pos);
      uint8_t bit=t=="f"?1:2;if(functions&bit)return fail(Error::Circular,pos);
      const auto& source=t=="f"?q.memory.f:q.memory.g;
      if(source.empty())return fail(Error::NotDefined,pos);
      const auto p=parse(source);if(p.error!=Error::None)return fail(p.error,pos);
      functions|=bit;auto saved=r.memory.variables[6];r.memory.variables[6]=a[0];
      auto value=eval(p.root);r.memory.variables[6]=saved;functions&=~bit;return value;
    }
    if(t=="frac" && a.size()==2)return combine(a[0],a[1],'/');
    if(t=="pow" && a.size()==2)return power(a[0],a[1],pos);
    if(t=="mixed" && a.size()==3)return combine(a[0],combine(a[1],a[2],'/'),'+');
    if(t=="root" && a.size()==2) {
      Decimal d;int64_t k;
      if(x.toInt(k)&&k>=2&&k<=kFastRootIndex&&(!a[1].number.negative()||k%2)) {
        d=a[1].number.abs().root(static_cast<unsigned>(k));
        if(a[1].number.negative())d=-d;
      } else {
        d=a[1].number.pow(Decimal(1)/x);
        if(a[1].number.negative() && x.integer()) {if(x.toInt(k)&&k%2)d=-(-a[1].number).pow(Decimal(1)/x);}
      }
      if(a[0].symbolic.exact()&&a[1].symbolic.exact()) {
        const ExactNumber s=a[0].symbolic.isInteger()&&a[0].symbolic.rationalValue().sign()>0&&
                            a[0].symbolic.rationalValue().num()<=BigInt(ExactNumber::kMaxIndex)
                            ?a[1].symbolic.root(static_cast<uint32_t>(a[0].symbolic.rationalValue().num().toInt64()))
                            :a[1].symbolic.pow(ExactNumber::integer(1)/a[0].symbolic);
        if(s.status()!=ExactStatus::Inexact)return settle(s,d);
      }
      return Value::approximate(d);
    }
    if(t=="log" && (a.size()==1||a.size()==2)) {
      const Decimal d=a.size()==1?x.ln()/Decimal(10).ln():a[1].number.ln()/x.ln();
      const Value& arg=a.size()==1?a[0]:a[1];
      const ExactNumber base=a.size()==1?ExactNumber::integer(10):a[0].symbolic;
      if(arg.symbolic.exact()&&base.exact()) {
        const ExactNumber s=arg.symbolic.log(base);
        if(s.status()!=ExactStatus::Inexact)return settle(s,d);
      }
      return Value::approximate(d);
    }
    if(t=="gcd"||t=="lcm"||t=="randint"||t=="rem") {
      if(a.size()!=2)return fail(Error::Argument,pos);
      int64_t u,v;if(!x.toInt(u)||!a[1].number.toInt(v)||x.abs()>=Decimal("1E10")||a[1].number.abs()>=Decimal("1E10"))return fail(Error::Math,pos);
      if(t=="gcd")return Value(Decimal(std::to_string(std::gcd(u,v))));
      if(t=="lcm") {if(u<0||v<0)return fail(Error::Math,pos);if(!u||!v)return Value(0);return Value(x/Decimal(std::to_string(std::gcd(u,v)))*a[1].number);}
      if(t=="randint") {if(u>=v||v-u>=10000000000LL)return fail(Error::Math,pos);
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;uint64_t bits=rng;rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;bits=(bits<<32)|rng;
        return Value(Decimal(std::to_string(u+static_cast<int64_t>(bits%static_cast<uint64_t>(v-u+1)))));}
      if(!v)return fail(Error::Math,pos);
      auto quotient=(x/a[1].number).trunc(),remainder=x-quotient*a[1].number;
      if(quotient.negative()||remainder.negative())return Value(x/a[1].number);
      r.pair=true;r.second=Value(remainder);r.memory.variables[4]=Value(quotient);r.memory.variables[5]=r.second;return Value(quotient);
    }
    if(t=="pol"||t=="rec") {
      if(a.size()!=2)return fail(Error::Argument,pos);
      Decimal y=a[1].number;Value first,second;
      if(t=="pol") {
        if(a[0].isZero()&&a[1].isZero())return fail(Error::Math,pos);
        first=realSqrt(combine(combine(a[0],a[0],'*'),combine(a[1],a[1],'*'),'+'));
        Decimal theta=x.zero()?(y.negative()?-Decimal::pi()/2:Decimal::pi()/2):(y/x).atan();
        if(x.negative())theta=theta+(y.negative()?-Decimal::pi():Decimal::pi());
        int64_t degrees;
        second=exactArgument(a[0],a[1],degrees)?settle(exactAngle(degrees,q.settings.angle),angle(theta)):Value::approximate(angle(theta));
      }else {
        if(x.negative())return fail(Error::Math,pos);
        first=combine(a[0],realTrig(false,a[1],q.settings.angle),'*');second=combine(a[0],realTrig(true,a[1],q.settings.angle),'*');
      }
      r.pair=true;r.second=second;r.memory.variables[6]=first;r.memory.variables[7]=r.second;return first;
    }
    if(t=="dms" && a.size()==3)return combine(combine(a[0],combine(a[1],Value(60),'/'),'+'),combine(a[2],Value(3600),'/'),'+');
    if(t=="repeat" && a.size()==2) {int64_t places;if(!a[1].number.toInt(places)||places<1||places>10||x<0||!x.integer()||x>=Decimal(10).pow(a[1].number))return fail(Error::Argument,pos);return combine(a[0],Value(Decimal(10).pow(a[1].number)-1),'/');}
    if(a.size()!=1)return fail(Error::Argument,pos);
    if(t=="sqrt")return realSqrt(a[0]);
    if(t=="abs"){
      if(a[0].symbolic.exact()){const ExactNumber s=a[0].symbolic.abs();if(s.exact())return settle(s,x.abs());}
      return Value::approximate(x.abs());
    }
    if(t=="ln"||t=="exp") {
      const Decimal d=t=="ln"?x.ln():x.exp();
      if(a[0].symbolic.exact()){const ExactNumber s=t=="ln"?a[0].symbolic.ln():a[0].symbolic.exp();if(s.status()!=ExactStatus::Inexact)return settle(s,d);}
      return Value::approximate(d);
    }
    if((t=="int"||t=="floor") && a[0].symbolic.isRational()) {
      const Rational& v=a[0].symbolic.rationalValue();
      const BigInt f=v.floor();
      return Value::of(ExactNumber::integer(t=="floor"||v.sign()>=0||v.integer()?f:f+BigInt(1)));
    }
    if(t=="int")return Value(x.trunc());if(t=="floor")return Value(x.floor());
    if(t=="rnd")return Value(q.settings.number==NumberFormat::Fix?x.quantized(q.settings.digits):x.rounded(q.settings.number==NumberFormat::Sci?q.settings.digits:10));
    if(t=="deg")return fromRadians(combine(combine(a[0],piValue(),'*'),Value(180),'/'));
    if(t=="rad")return fromRadians(a[0]);
    if(t=="grad")return fromRadians(combine(combine(a[0],piValue(),'*'),Value(200),'/'));
    if(t=="sin"||t=="cos"||t=="tan") {
      const auto theta=rad(x);
      if(theta.abs()>=Decimal("157079632.6794896619231321691639751442099"))return fail(Error::Math,pos);
      if(t!="tan")return realTrig(t=="sin",a[0],q.settings.angle);
      if((theta.abs()%Decimal::pi()-Decimal::pi()/2).abs()<Decimal("1E-32"))return fail(Error::Math,pos);
      Decimal y=theta.sin()/theta.cos();
      if(y.abs()<Decimal("1E-32"))y=0;
      int64_t degrees;
      if(exactDegrees(a[0],q.settings.angle,degrees)) {
        ExactNumber s,c;
        if(standardTrig(true,degrees,s)&&standardTrig(false,degrees,c)&&!c.zero())return settle(s/c,y);
      }
      return Value::approximate(y);
    }
    if(t=="atan"||t=="asin"||t=="acos") {
      if(t!="atan"&&x.abs()>1)return fail(Error::Math,pos);
      Decimal y=t=="atan"?x.atan():x.abs()==1?(x.negative()?-Decimal::pi()/2:Decimal::pi()/2):(x/(1-x*x).sqrt()).atan();
      if(t=="acos")y=Decimal::pi()/2-y;
      int64_t degrees;
      if(inverseTrigDegrees(t,a[0].symbolic,degrees))return settle(exactAngle(degrees,q.settings.angle),angle(y));
      return Value::approximate(angle(y));
    }
    if(t=="sinh")return Value((x.exp()-(-x).exp())/2);
    if(t=="cosh")return Value((x.exp()+(-x).exp())/2);
    if(t=="tanh"){if(x>50)return Value(1);if(x< -50)return Value(-1);auto e=(x*2).exp();return Value((e-1)/(e+1));}
    if(t=="asinh")return Value((x+(x*x+1).sqrt()).ln());
    if(t=="acosh"){if(x<1)return fail(Error::Math,pos);return Value((x+(x*x-1).sqrt()).ln());}
    if(t=="atanh"){if(x.abs()>=1)return fail(Error::Math,pos);return Value(((1+x)/(1-x)).ln()/2);}
    return fail(Error::Syntax,pos);
  }
  // An exact p/q exponent with a modest q goes through the q-th root and an
  // integer power (Decimal::root): exp(ln) per node made x^(1/3) in an
  // integrand ~35x slower than sqrt(x).
  static constexpr int64_t kFastRootIndex=100,kFastRootPower=1000000;
  Value power(const Value& a,const Value& b,size_t pos) {
    Decimal d;
    bool rational=b.symbolic.isRational()&&!b.symbolic.isInteger();
    int64_t num=0,den=0;
    if(rational) {
      const auto& v=b.symbolic.rationalValue();
      rational=v.den()<=BigInt(kFastRootIndex)&&v.num()<=BigInt(kFastRootPower)&&v.num()>=BigInt(-kFastRootPower);
      if(rational){num=v.num().toInt64();den=v.den().toInt64();}
    }
    // A negative base with an odd-denominator exponent has a real root.
    if(rational&&(!a.number.negative()||den%2)) {
      d=a.number.abs().root(static_cast<unsigned>(den)).pow(Decimal(static_cast<int>(num)));
      if(a.number.negative()&&num%2)d=-d;
    } else {
      d=a.number.pow(b.number);
      if(a.number.negative()&&b.symbolic.isRational()&&!b.symbolic.isInteger()&&b.symbolic.rationalValue().den().odd()) {
        d=(-a.number).pow(b.number);if(b.symbolic.rationalValue().num().odd())d=-d;
      }
    }
    if(a.symbolic.exact()&&b.symbolic.exact()) {
      const ExactNumber s=a.symbolic.pow(b.symbolic);
      if(s.status()!=ExactStatus::Inexact)return checked(settle(s,d),pos);
    }
    return checked(Value::approximate(d),pos);
  }
  // A value in radians, in the current angle unit (exactly: 30 degrees stays 30).
  Value fromRadians(const Value& radians) {
    if(q.settings.angle==Angle::Radian)return radians;
    return combine(combine(radians,Value(q.settings.angle==Angle::Degree?180:200),'*'),piValue(),'/');
  }
  Value perm(const Value& a,const Value& b,bool combination,size_t pos) {
    int64_t n,k;if(!a.number.toInt(n)||!b.number.toInt(k)||k<0||n<k||n>=10000000000LL)return fail(Error::Math,pos);
    if(combination)k=std::min(k,n-k);
    Value value(1);
    for(int64_t j=1;j<=k && tick();++j) {
      value=combine(value,Value(Decimal(std::to_string(n-j+1))),'*');
      if(combination)value=combine(value,Value(Decimal(std::to_string(j))),'/');
      if(value.number>Decimal("9.999999999E99"))return fail(Error::Math,pos);
    }return value;
  }
  Decimal at(const Expression& fn,const Decimal& x) {
    auto saved=r.memory.variables[6];r.memory.variables[6]=Value::approximate(x);auto v=eval(fn);r.memory.variables[6]=saved;return v.number;
  }
  Value analysis(const Expression& n) {
    const auto& a=n->children;const auto& t=n->text;const auto p=n->position;
    if(t=="diff") {
      if(a.size()<2||a.size()>3)return fail(Error::Argument,p);
      Decimal x=eval(a[1]).number,tol=a.size()==3?eval(a[2]).number:Decimal("1E-16");
      if(tol<Decimal("1E-22")||tol>=1)return fail(Error::Argument,p);
      Decimal h=(x.abs()+1)*Decimal("0.01"),previous=Decimal::nan();
      for(int i=0;i<32 && tick();++i) {
        Decimal d=(at(a[0],x+h)-at(a[0],x-h))/(h*2);
        Decimal half=(at(a[0],x+h/2)-at(a[0],x-h/2))/h;
        Decimal result=(half*4-d)/3;
        if(previous.finite()&&(result-previous).abs()<=tol)return Value::approximate(result);
        previous=result;h=h/4;
      }
      return fail(Error::TimeOut,p);
    }
    if(a.size()<3||a.size()>4)return fail(Error::Argument,p);
    const Value lowValue=eval(a[1]),highValue=eval(a[2]);
    Decimal low=lowValue.number,high=highValue.number;
    if(t=="sum"||t=="product") {
      int64_t first,last;if(a.size()!=3||!low.toInt(first)||!high.toInt(last)||first>last||low.abs()>=Decimal("1E10")||high.abs()>=Decimal("1E10"))return fail(Error::Argument,p);
      Value value(t=="sum"?0:1);auto saved=r.memory.variables[6];
      for(int64_t i=first;i<=last&&tick();++i) {r.memory.variables[6]=Value(Decimal(std::to_string(i)));value=checked(combine(value,eval(a[0]),t=="sum"?'+':'*'),p);}
      r.memory.variables[6]=saved;return value;
    }
    Decimal tol=a.size()==4?eval(a[3]).number:Decimal("1E-10");
    if(tol<Decimal("1E-22")||tol>=1)return fail(Error::Argument,p);
    if(r.error!=Error::None)return Value();
    // An integrand may hold an integral: keep the outer one's mapping.
    const Mapping savedMapping=mapping;const Decimal savedOrigin=origin;
    // Exact bounds look for the exact forms first (the polynomial, then the
    // CAS's): with one in hand the numeric value is only its check, and a
    // rough one (kRoughTolerance) checks as well -- past the rough digits a
    // cusp (x^(1/3) at 0) costs several bisections per digit. A rough
    // disagreement is one at tol too, so tol is spent only where no exact
    // form exists or none agrees, and then nothing is tried twice.
    ExactNumber candidates[2];
    bool found[2]={false,false};
    const bool exactBounds=lowValue.symbolic.exact()&&highValue.symbolic.exact();
    if(exactBounds) {
      found[0]=polynomialIntegral(a[0],lowValue,highValue,candidates[0]);
      found[1]=!found[0]&&symbolicIntegral(a[0],lowValue,highValue,candidates[1]);
    }
    if(r.error!=Error::None)return Value();
    const Decimal roughTol(kRoughTolerance);
    Value exact;
    bool tried=false;
    if((found[0]||found[1])&&tol<roughTol) {
      Decimal rough;
      const bool done=integral(a[0],low,high,roughTol,rough);
      mapping=savedMapping;origin=savedOrigin;
      if(r.error!=Error::None)return Value();
      if(done) {
        tried=true;
        // A polynomial fooled by its samples (sin(180x) on whole degrees)
        // still leaves the CAS its turn.
        if(found[0]&&agrees(candidates[0],rough,true,exact))return exact;
        if(found[0]){found[1]=symbolicIntegral(a[0],lowValue,highValue,candidates[1]);if(r.error!=Error::None)return Value();}
        if(found[1]&&agrees(candidates[1],rough,true,exact))return exact;
      }
    }
    Decimal value;
    const bool done=integral(a[0],low,high,tol,value);
    mapping=savedMapping;origin=savedOrigin;
    if(r.error!=Error::None)return Value();
    if(!done)return fail(Error::TimeOut,p);
    if(!tried) {
      if(found[0]&&agrees(candidates[0],value,false,exact))return exact;
      if(exactBounds&&found[0])found[1]=symbolicIntegral(a[0],lowValue,highValue,candidates[1]);
      if(found[1]&&agrees(candidates[1],value,false,exact))return exact;
    }
    return Value::approximate(value);
  }
  // An exact candidate is kept only where it agrees with the numeric value:
  // to 1E-8 relative against a value settled to tol, to 1E-6 against a rough
  // one (kRoughTolerance), whose ~7 digits also make the exact form's own
  // decimal the better twin.
  static constexpr const char* kRoughTolerance="1E-7";
  bool agrees(const ExactNumber& exact,const Decimal& numeric,bool rough,Value& out) {
    const Decimal d=exact.toDecimal();
    if(!d.finite()||(d-numeric).abs()>Decimal(rough?"1E-6":"1E-8")*(numeric.abs()+1))return false;
    out=settle(exact,rough?d:numeric);
    return true;
  }
  // A polynomial integrand on exact bounds integrates exactly, so
  // integral(9-4x,0,9/4) is 81/8 and stays exact through *3*sqrt(2). Ten
  // equally spaced exact samples; when their finite differences vanish past
  // degree d <= 8, Newton-Cotes over the interpolant is the exact integral.
  // Samples alone can be fooled (sin(pi*x) on whole numbers), so the result
  // is a candidate, kept only where it agrees with the numeric value; anything
  // else -- an inexact sample, an error at a sample point -- gives none.
  bool polynomialIntegral(const Expression& f,const Value& low,const Value& high,ExactNumber& out) {
    constexpr int kPoints=10;
    if(!low.symbolic.exact()||!high.symbolic.exact()||low.symbolic==high.symbolic)return false;
    const ExactNumber step=(high.symbolic-low.symbolic)/ExactNumber::integer(BigInt(kPoints-1));
    if(!step.exact())return false;
    const Error savedError=r.error;const auto savedPosition=r.position;
    const auto saved=r.memory.variables[6];
    std::vector<ExactNumber> diff;
    for(int i=0;i<kPoints&&r.error==Error::None;++i) {
      const ExactNumber x=low.symbolic+step*ExactNumber::integer(BigInt(i));
      if(!x.exact())break;
      r.memory.variables[6]=Value::of(x);
      const Value y=eval(f);
      if(r.error!=Error::None||!y.symbolic.exact())break;
      diff.push_back(y.symbolic);
    }
    r.memory.variables[6]=saved;
    const bool sampled=r.error==Error::None&&diff.size()==kPoints;
    if(r.error!=Error::Cancelled&&r.error!=Error::TimeOut){r.error=savedError;r.position=savedPosition;}
    if(!sampled)return false;
    // In place: diff[k] becomes the k-th forward difference at the first point.
    int degree=-1;
    for(int k=1;k<kPoints;++k) {
      bool vanished=true;
      for(int i=kPoints-1;i>=k;--i) {
        diff[i]=diff[i]-diff[i-1];
        if(!diff[i].exact())return false;
        if(!diff[i].zero())vanished=false;
      }
      if(vanished){degree=k-1;break;}
    }
    if(degree<0)return false;
    // Weight of the k-th difference: integral over s in [0, n] of C(s, k),
    // n = kPoints-1, from C(s, k)'s coefficients in s.
    ExactNumber sum=ExactNumber::integer(BigInt(0));
    std::vector<Rational> binomial{Rational(1)};
    for(int k=0;k<=degree;++k) {
      if(k>0) {
        std::vector<Rational> next(binomial.size()+1,Rational(0));
        for(size_t j=0;j<binomial.size();++j) {
          next[j+1]=next[j+1]+binomial[j];
          next[j]=next[j]-binomial[j]*Rational(k-1);
        }
        for(auto& c:next)c=c/Rational(k);
        binomial=next;
      }
      Rational weight(0),power(kPoints-1);
      for(size_t j=0;j<binomial.size();++j) {
        weight=weight+binomial[j]*power/Rational(static_cast<int>(j+1));
        power=power*Rational(kPoints-1);
      }
      sum=sum+ExactNumber::rational(weight)*diff[k];
    }
    out=step*sum;
    return out.exact();
  }
  // The CAS's definite integral (calculator_cas_integrate.h): |g| split at its
  // zeros, F(b)-F(a) on each piece, so integral(4-4abs(cos(x)),0,pi) is
  // 4pi-8 and integral(e^x,0,1) is e-1. A candidate too, kept only where it
  // agrees with the numeric value: an antiderivative with a jump
  // inside the interval (or a missed zero of |g|) gives a wrong difference.
  bool symbolicIntegral(const Expression& f,const Value& low,const Value& high,ExactNumber& out) {
    if(!exactIntegralHook||!low.symbolic.exact()||!high.symbolic.exact())return false;
    return exactIntegralHook(f,low.symbolic,high.symbolic,q.settings,r.memory,cancel,cooperate,out)&&out.exact();
  }
  // Numeric integration after NumWorks Poincare (ApproximateIntegral):
  // - a bound beyond +-1000 maps its side onto a finite s-interval,
  //   x = origin +- s/(1-s), so e^(-x^2) over [0, 100000] keeps its mass in
  //   view instead of between two nodes;
  // - an integrand undefined at an end (1/sqrt(x), ln(x)) goes to tanh-sinh;
  // - otherwise globally adaptive Gauss-Kronrod, with tanh-sinh as the
  //   fallback when it cannot settle (sqrt-like ends such as sqrt(x)).
  // False is Time Out: no estimate met the tolerance.
  enum class Mapping : uint8_t {None,Right,Left};
  Mapping mapping=Mapping::None;Decimal origin;
  bool integral(const Expression& f,const Decimal& lo,const Decimal& hi,const Decimal& tol,Decimal& out) {
    if(lo==hi){out=Decimal(0);return true;}
    if(hi<lo){const bool ok=integral(f,hi,lo,tol,out);out=-out;return ok;}
    static const Decimal far("1000");
    if(lo< -far&&hi>far) {
      Decimal left,right;
      if(!integral(f,lo,Decimal(0),tol/2,left)||!integral(f,Decimal(0),hi,tol/2,right))return false;
      out=left+right;return true;
    }
    Decimal a=lo,b=hi;
    mapping=Mapping::None;
    if(hi>far||lo< -far) {
      mapping=hi>far?Mapping::Right:Mapping::Left;
      origin=hi>far?lo:hi;
      a=Decimal(0);b=(hi-lo)/(hi-lo+1);
    }
    // Where tanh-sinh cannot settle an end singularity (1/x, x^-0.9),
    // Gauss-Kronrod, whose nodes stay further from the end, cannot either.
    const bool singularEnd=!definedAt(f,a)||!definedAt(f,b);
    if(r.error!=Error::None)return false;
    if(singularEnd)return tanhSinh(f,a,b,tol,out);
    if(kronrod(f,a,b,tol,out))return true;
    return r.error==Error::None&&tanhSinh(f,a,b,tol,out);
  }
  // f(x(s))*x'(s): the integrand in the mapped variable.
  Decimal point(const Expression& f,const Decimal& s) {
    if(mapping==Mapping::None)return at(f,s);
    const Decimal u=1-s,t=s/u;
    return at(f,mapping==Mapping::Right?origin+t:origin-t)/(u*u);
  }
  // f in a sandbox: a Math ERROR (outside the domain) is false and leaves no
  // error behind; a stop (AC, time out) stays.
  bool sample(const Expression& f,const Decimal& s,Decimal& y) {
    y=point(f,s);
    if(r.error!=Error::Math)return r.error==Error::None;
    r.error=Error::None;r.position=0;return false;
  }
  bool definedAt(const Expression& f,const Decimal& s) {Decimal y;return sample(f,s,y);}
  // Tanh-sinh (double-exponential) quadrature over [lo, hi]: x = c -+ r*tanh(u),
  // u = pi/2*sinh(t), weight r*pi/2*cosh(t)/cosh(u)^2, trapezoid in t with
  // the step halved per level. Nodes crowd both ends double-exponentially and
  // never touch them, so integrable end singularities converge in a few
  // levels. Each node is placed by its distance to its end,
  // d = (hi-lo)/(e^(2u)+1), so one 1E-35 from an end is not rounded onto it.
  // A side stops where its terms no longer matter, where x rounds onto the
  // end, or where f is undefined that close to it (the tail left out is below
  // the working precision for an integrable singularity).
  bool tanhSinh(const Expression& f,const Decimal& lo,const Decimal& hi,const Decimal& tol,Decimal& out) {
    static const Decimal halfPi=Decimal::pi()/2,tiny("1E-45");
    const Decimal width=hi-lo;
    if(width.zero()){out=Decimal(0);return true;}
    Decimal fc;
    if(!sample(f,(lo+hi)/2,fc))return false;
    // `sum` holds the weighted terms of every node so far; the estimate at
    // step h is h*sum. Level 0 (h = 1) takes t = 0, +-1, +-2, ...; level k
    // adds the odd multiples of 2^-k.
    Decimal sum=fc*halfPi*width/2,previous,h(1);
    for(int level=0;level<=7;++level) {
      Decimal added(0);
      const Decimal step=level?h*2:h,growth=step.exp();  // e^t advances by a product
      bool left=true,right=true;
      Decimal et=h.exp();
      for(Decimal t=h;(left||right)&&t<Decimal(5);t=t+step,et=et*growth) {
        if(!tick())return false;
        const Decimal u=halfPi*(et-1/et)/2,e2u=(u*2).exp();
        const Decimal d=width/(e2u+1),w=halfPi*(et+1/et)/2*4*e2u/((e2u+1)*(e2u+1))*width/2;
        if((w*(fc.abs()+1)).abs()<tiny*(sum.abs()+1)&&t>Decimal(1)){left=right=false;break;}
        Decimal y;
        const Decimal xl=lo+d,xr=hi-d;
        if(left&&(xl==lo||!sample(f,xl,y)))left=false;
        if(r.error!=Error::None)return false;
        if(left)added=added+w*y;
        if(right&&(xr==hi||!sample(f,xr,y)))right=false;
        if(r.error!=Error::None)return false;
        if(right)added=added+w*y;
      }
      sum=sum+added;
      const Decimal estimate=sum*h;
      if(level>=3&&(estimate-previous).abs()<=tol){out=estimate;return true;}
      previous=estimate;h=h/2;
    }
    return false;
  }
  // One embedded Gauss 7 / Kronrod 15 pair over [lo, hi]: the Kronrod value,
  // and |K - G| as its error estimate. Decimal constants retain guard digits.
  struct Piece {Decimal lo,hi,value,error;};
  Piece gaussKronrod(const Expression& f,const Decimal& lo,const Decimal& hi) {
    static const char* nodes[]={"0.991455371120812639206854697526329","0.949107912342758524526189684047851","0.864864423359769072789712788640926","0.741531185599394439863864773280788","0.586087235467691130294144838258730","0.405845151377397166906606412076961","0.207784955007898467600689403773245"};
    static const char* wk[]={"0.022935322010529224963732008058970","0.063092092629978553290700663189204","0.104790010322250183839876322541518","0.140653259715525918745189590510238","0.169004726639267902826583426598550","0.190350578064785409913256402421014","0.204432940075298892414161999234649"};
    static const char* wg[]={"0.129484966168869693270611432679082","0.279705391489276667901467771423780","0.381830050505118944950369775488975"};
    const Decimal mid=(lo+hi)/2,half=(hi-lo)/2,center=point(f,mid);
    Decimal k=center*Decimal("0.209482141084727828012999174891714"),g=center*Decimal("0.417959183673469387755102040816327");
    for(int i=0;i<7 && tick();++i) {const auto offset=half*Decimal(nodes[i]);auto pair=point(f,mid-offset)+point(f,mid+offset);k=k+pair*Decimal(wk[i]);if(i%2)g=g+pair*Decimal(wg[i/2]);}
    k=k*half;g=g*half;
    return Piece{lo,hi,k,(k-g).abs()};
  }
  // Globally adaptive Gauss-Kronrod (QUADPACK's QAG, as Poincare does):
  // start from 8 pieces, so a narrow peak (710x*e^(-710x) on [0, 10]) is
  // seen, then bisect the piece with the largest error until the summed
  // estimate is within tol, or within 1E-15 of the value (e^x on [0, 200] is
  // 7E86: no absolute tolerance is reachable there). Work gathers where the
  // integrand is hard, so a kink costs a few bisections per digit instead of
  // halving the tolerance at every level. At most 128 pieces are kept (PSRAM).
  bool kronrod(const Expression& f,const Decimal& lo,const Decimal& hi,const Decimal& tol,Decimal& out) {
    static const Decimal relative("1E-15");
    std::vector<Piece,ExactAllocator<Piece>> pieces;
    pieces.reserve(128);
    const Decimal step=(hi-lo)/8;
    for(int i=0;i<8;++i)pieces.push_back(gaussKronrod(f,lo+step*i,i==7?hi:lo+step*(i+1)));
    while(r.error==Error::None) {
      Decimal value(0),error(0);
      size_t worst=0;
      for(size_t i=0;i<pieces.size();++i) {
        value=value+pieces[i].value;error=error+pieces[i].error;
        if(pieces[i].error>pieces[worst].error)worst=i;
      }
      if(error<=tol||error<=value.abs()*relative){out=value;return true;}
      if(pieces.size()>=128||!tick())return false;
      const Piece p=pieces[worst];
      const Decimal mid=(p.lo+p.hi)/2;
      if(mid==p.lo||mid==p.hi)return false;  // no room left to bisect
      pieces[worst]=gaussKronrod(f,p.lo,mid);
      pieces.push_back(gaussKronrod(f,mid,p.hi));
    }
    return false;
  }
};
}
Result evaluate(const Request& q,const std::function<bool()>& cancel,const std::function<void()>& cooperate) {
  return Evaluator(q,cancel,cooperate).run();
}
Result evaluateTree(const Expression& root,const Request& q,const std::function<bool()>& cancel,const std::function<void()>& cooperate) {
  return Evaluator(q,cancel,cooperate).runTree(root);
}
Result evaluateScalarOperation(const Expression& operation, const std::vector<Value>& arguments,
                               const Request& q, const std::function<bool()>& cancel,
                               const std::function<void()>& cooperate) {
  return Evaluator(q,cancel,cooperate).runOperation(operation, arguments);
}
void flattenRelations(const Expression& n,std::vector<Expression>& operands,std::vector<std::string>& relations) {
  if(isRelation(n)) {flattenRelations(n->children[0],operands,relations);relations.push_back(n->text);operands.push_back(n->children[1]);}
  else operands.push_back(n);
}
Error verifyChainError(const std::vector<std::string>& relations) {
  if(relations.empty()) return Error::NoOperator;
  int direction=0; bool equality=false,ordered=false;
  for(const auto& rel:relations) {
    const int d=rel[0]=='<'?1:rel[0]=='>'?-1:0;
    if(d && direction && d!=direction) return Error::Syntax;
    if(d) {direction=d;ordered=true;} else equality=true;
  }
  // = or != inside an ordered chain (1<2=2) has no single reading.
  return ordered&&equality?Error::Syntax:Error::None;
}
Exact exactRational(int64_t n,int64_t d,int64_t rad,bool pi) {return rational(n,d,rad,pi);}
ExactNumber exactNumber(const Exact& e) {
  if(!e.valid)return ExactNumber();
  const ExactNumber c=ExactNumber::fraction(e.numerator,e.denominator);
  return e.pi?c*ExactNumber::pi():e.euler?c*ExactNumber::e():e.radicand!=1?c*ExactNumber::integer(e.radicand).sqrt():c;
}
Value realCombine(const Value& a,const Value& b,char op) {return combine(a,b,op);}
Value realNegate(const Value& a) {
  Value v=a;v.number=-v.number;v.symbolic=-a.symbolic;return v;
}
Value realSqrt(const Value& a) {
  const Decimal d=a.number.sqrt();
  if(a.symbolic.exact()){const ExactNumber s=a.symbolic.sqrt();if(s.status()!=ExactStatus::Inexact)return settle(s,d);}
  return Value::approximate(d);
}
int compareValues(const Value& a,const Value& b) {
  if(a.symbolic.exact()&&b.symbolic.exact()) {
    bool certain=false;const int c=ExactNumber::compare(a.symbolic,b.symbolic,&certain);
    if(certain)return c;
  }
  return a.number.compare(b.number);
}
std::string approximate(const Value& v,unsigned digits) {
  if(v.symbolic.exact())return v.symbolic.approximate(digits);
  if(!v.number.finite())return "";
  return v.number.str(std::max(1u,std::min(digits,40u)));
}
Value realTrig(bool sine,const Value& angle,Angle unit) {
  const Decimal& x=angle.number;
  const Decimal theta=unit==Angle::Degree?x*Decimal::pi()/180:unit==Angle::Gradian?x*Decimal::pi()/200:x;
  Decimal y=sine?theta.sin():theta.cos();
  if(y.abs()<Decimal("1E-32"))y=0;
  // Exact standard angles are recognized by their input, never by guessing
  // an arbitrary computed decimal's closeness to a rational.
  // The exact angle decides (pi/4 stays pi/4 whatever its decimal rounds
  // to); an inexact angle keeps the legacy whole-degree reading of its decimal.
  int64_t degrees;
  bool known=exactDegrees(angle,unit,degrees);
  if(!known && !angle.symbolic.exact() && unit!=Angle::Radian)
    known=(unit==Angle::Degree?x%360:(x*Decimal("0.9"))%360).toInt(degrees);
  ExactNumber exact;
  if(known && standardTrig(sine,degrees,exact))return settle(exact,y);
  return Value::approximate(y);
}

std::string formatValue(const Value& v,const Settings& s,Format f) {
  const Exact e=v.exact(),extra=v.extra();
  if(f==Format::Standard && (s.io==IO::MathDecimal||s.io==IO::LineDecimal))f=Format::Decimal;
  if(f==Format::Standard&&s.mixed)f=Format::Mixed;
  auto permitted=[&](const Exact& term){
    if(term.constant())return v.number.abs()<Decimal("1E6");
    if(term.radicand==1)return true;
    return radicalTermFits(term);
  };
  if(e.valid&&(!permitted(e)||(extra.valid&&(!permitted(extra)||!twoTermFits(e,extra)))))f=Format::Decimal;
  if(extra.valid&&(f==Format::Standard||f==Format::Mixed||f==Format::Improper)){
    if(s.io==IO::MathMath||s.io==IO::MathDecimal){
      Value first=Value::of(exactNumber(e)),second=Value::of(exactNumber(extra));
      auto a=formatValue(first,s,f),b=formatValue(second,s,f);
      return a+(b[0]=='-'?"":"+")+b;
    }
    f=Format::Decimal;
  }
  if((f==Format::Standard||f==Format::Mixed||f==Format::Improper)&&e.valid) {
    if(e.constant()||e.radicand!=1) {
      if(s.io==IO::MathMath||s.io==IO::MathDecimal) {
        std::string text=e.numerator==1?"":e.numerator==-1?"-":std::to_string(e.numerator)+"*";
        text+=e.pi?"pi":e.euler?"e":"sqrt("+std::to_string(e.radicand)+")";
        if(e.denominator!=1)text+="/"+std::to_string(e.denominator);return text;
      }
    } else {
      const auto n=std::to_string(e.numerator),d=std::to_string(e.denominator);
      if(e.denominator==1)return n;
      const auto mixedDigits=std::abs(e.numerator)>e.denominator?std::to_string(std::abs(e.numerator)/e.denominator).size()+std::to_string(std::abs(e.numerator)%e.denominator).size()+d.size()+2:n.size()+d.size()+1;
      if(mixedDigits<=10) {
        if(f==Format::Mixed && std::abs(e.numerator)>e.denominator)return std::to_string(e.numerator/e.denominator)+" "+std::to_string(std::abs(e.numerator)%e.denominator)+"/"+d;
        return n+"/"+d;
      }
    }
  }
  if(f==Format::Recurring && e.valid && e.radicand==1 && !e.constant()) {
    int64_t n=std::abs(e.numerator),d=e.denominator,rem=n%d;
    std::string prefix=(e.numerator<0?"-":"")+std::to_string(n/d),digits;
    std::vector<int64_t> remainders;
    while(rem && digits.size()<100) {
      auto it=std::find(remainders.begin(),remainders.end(),rem);
      if(it!=remainders.end()) {size_t start=it-remainders.begin();return prefix+"."+digits.substr(0,start)+"("+digits.substr(start)+")";}
      remainders.push_back(rem);rem*=10;digits+=char('0'+rem/d);rem%=d;
    }
    if(!rem)return prefix+(digits.empty()?"":"."+digits);
  }
  if(f==Format::Prime) {
    int64_t n;if(v.number.toInt(n)&&n>0&&n<10000000000LL) {
      if(n==1)return "1";
      std::string text;unsigned large=0;
      for(int64_t p=2;p<=1009&&p*p<=n;++p) {
        if(n%p)continue;if(p>999 && ++large>1)break;
        unsigned power=0;while(n%p==0){n/=p;++power;}
        if(!text.empty())text+="*";text+=std::to_string(p);if(power>1)text+="^"+std::to_string(power);
      }
      if(n>1){if(!text.empty())text+="*";text+=n>=1018081?"("+std::to_string(n)+")":std::to_string(n);}return text;
    }
  }
  if(f==Format::Sexagesimal) {
    auto x=v.number.abs();int64_t d,m;if(x.floor().toInt(d)&&d<=9999999&&((x-x.floor())*60).floor().toInt(m)) {
      const auto seconds=((x-Decimal(std::to_string(d)))*3600-Decimal(static_cast<int>(m))*60).quantized(2);
      return std::string(v.number.negative()?"-":"")+std::to_string(d)+"d"+std::to_string(m)+"m"+seconds.str(10)+"s";
    }
  }
  Decimal d=v.number;
  if(s.number==NumberFormat::Fix)d=d.quantized(s.digits);
  unsigned digits=s.number==NumberFormat::Sci?std::max<unsigned>(1,s.digits):10;
  std::string text=d.str(digits);
  // Decimal output is expanded according to Norm1/Norm2, not decNumber's
  // general-purpose six-place threshold.
  if(f==Format::Engineering || s.engineer || s.number==NumberFormat::Sci ||
     (!d.zero() && (d.abs()>=Decimal("1E10")||d.abs()<Decimal(s.number==NumberFormat::Norm2?"1E-9":"1E-2")))) {
    if(!d.zero()) {
      int exponent=0;Decimal mantissa=d;
      while(mantissa.abs()>=10){mantissa=mantissa/10;++exponent;}
      while(mantissa.abs()<1){mantissa=mantissa*10;--exponent;}
      if(f==Format::Engineering||s.engineer)while(exponent%3){mantissa=mantissa*10;--exponent;}
      text=mantissa.str(digits)+"E"+(exponent>=0?"+":"")+std::to_string(exponent);
    }
  } else {
    auto ep=text.find('E');
    if(ep!=std::string::npos) {
      int exponent=std::atoi(text.c_str()+ep+1);std::string mantissa=text.substr(0,ep);bool negative=mantissa[0]=='-';if(negative)mantissa.erase(0,1);
      auto dot=mantissa.find('.');int point=dot==std::string::npos?static_cast<int>(mantissa.size()):static_cast<int>(dot);if(dot!=std::string::npos)mantissa.erase(dot,1);point+=exponent;
      if(point<=0)text="0."+std::string(-point,'0')+mantissa;
      else if(point>=static_cast<int>(mantissa.size()))text=mantissa+std::string(point-mantissa.size(),'0');
      else text=mantissa.substr(0,point)+"."+mantissa.substr(point);
      if(negative)text="-"+text;
    }
    if(s.number==NumberFormat::Fix) {auto dot=text.find('.');if(dot==std::string::npos && s.digits) {text+='.';dot=text.size()-1;}
      if(dot!=std::string::npos)while(text.size()-dot-1<s.digits)text+='0';}
  }
  if(s.comma)std::replace(text.begin(),text.end(),'.',',');
  return text;
}
}  // namespace Calculator
