#include "ast.h"

#include <charconv>
#include <ostream>

namespace bant {
IntScalar *IntScalar::FromLiteral(Arena *arena, std::string_view literal) {
  int64_t val = 0;
  auto result = std::from_chars(literal.begin(), literal.end(), val);
  if (result.ec != std::errc{}) {
    return nullptr;
  }
  return arena->New<IntScalar>(val);
}

StringScalar *StringScalar::FromLiteral(Arena *arena,
                                        std::string_view literal) {
  if (literal.length() >= 6 && literal.substr(0, 3) == "\"\"\"") {
    literal = literal.substr(3);
    literal.remove_suffix(3);
  } else {
    literal = literal.substr(1);
    literal.remove_suffix(1);
  }

  // TODO: backslash escape removal.
  return arena->New<StringScalar>(literal);
}

void PrintVisitor::VisitAssignment(Assignment *a) {
  out_ << a->identifier()->id() << " = ";
  BaseVisitor::VisitAssignment(a);
}

void PrintVisitor::VisitFunCall(FunCall *f) {
  out_ << f->identifier()->id();
  BaseVisitor::VisitFunCall(f);
  out_ << "\n" << std::string(indent_, ' ');
}

void PrintVisitor::VisitList(List *l) {
  static constexpr int kIndentSpaces = 4;
  switch (l->type()) {
  case List::Type::kList: out_ << "["; break;
  case List::Type::kMap: out_ << "{"; break;
  case List::Type::kTuple: out_ << "("; break;
  }
  const bool needs_multiline = (l->size() > 1);
  if (needs_multiline) out_ << "\n";
  indent_ += kIndentSpaces;
  bool is_first = true;
  for (Node *node : *l) {
    if (!is_first) out_ << ",\n";
    if (needs_multiline) out_ << std::string(indent_, ' ');
    if (!WalkNonNull(node)) {
      out_ << "NIL";
    }
    is_first = false;
  }
  indent_ -= kIndentSpaces;
  if (needs_multiline) {
    out_ << "\n" << std::string(indent_, ' ');
  }

  switch (l->type()) {
  case List::Type::kList: out_ << "]"; break;
  case List::Type::kMap: out_ << "}"; break;
  case List::Type::kTuple: out_ << ")"; break;
  }
}

void PrintVisitor::VisitBinOpNode(BinOpNode *b) {
  WalkNonNull(b->left());
  if (b->op() == '.') {
    out_ << b->op();  // No spacing around dot operator.
  } else {
    out_ << " " << b->op() << " ";
  }
  WalkNonNull(b->right());
}

void PrintVisitor::VisitListComprehension(ListComprehension *lh) {
  out_ << "[\n";
  WalkNonNull(lh->pattern());
  out_ << "\n" << std::string(indent_, ' ') << "for ";
  WalkNonNull(lh->variable_list());
  out_ << "\n" << std::string(indent_, ' ') << "in ";
  WalkNonNull(lh->source());
  out_ << "\n]";
}

void PrintVisitor::VisitTernary(Ternary *t) {
  WalkNonNull(t->positive());
  out_ << " if ";
  WalkNonNull(t->condition());
  if (t->negative()) {
    out_ << " else ";
    t->negative()->Accept(this);
  }
}

void PrintVisitor::VisitScalar(Scalar *s) {
  if (s->type() == Scalar::kInt) {
    out_ << s->AsInt();
  } else {
    out_ << "\"" << s->AsString() << "\"";
  }
}

void PrintVisitor::VisitIdentifier(Identifier *i) { out_ << i->id(); }

std::ostream &operator<<(std::ostream &o, Node *n) {
  if (!PrintVisitor(o).WalkNonNull(n)) {
    o << "NIL";
  }
  return o;
}
}  // namespace bant
