// next steps
//  triple-string.

#include <ostream>
#include <iostream> // for main
#include <fstream>
#include <functional>
#include <charconv>

#include "scanner.h"
#include "ast.h"

class Parser {
public:
  Parser(const char *filename, Scanner *scanner)
    : filename_(filename), scanner_(scanner) {}

  // Attempt to parse. If there is an error, return at least partial tree.
  List *parse() {
    List *statement_list = new List(List::Type::kList);
    while (!error_) {
      auto tok = scanner_->Next();
      if (tok.type == kEof) {
        last_token_ = tok;
        return statement_list;
      }
      if (tok.type == kStringLiteral) {
        continue;  // Pythonism: Toplevel document no-effect statement
      }
      if (tok.type != kIdentifier) {
        MsgAt(tok) << "Expected identifier, got " << tok.text << "\n";
        SetErrorToken(tok);
        return statement_list;
      }

      // Got identifier, next step: either function call or assignment.
      auto after_id = scanner_->Next();
      switch (after_id.type) {
      case TokenType::kEquals:
        statement_list->Append(ParseAssignmentRhs(new Identifier(tok.text)));
        break;
      case TokenType::kOpenParen:
        statement_list->Append(ParseFunCall(tok));
        break;
      default:
        MsgAt(after_id)
          << "expected '(' or '=', got " << after_id.text << "\n";
        SetErrorToken(tok);
        return statement_list;
      }
    }
    return statement_list;
  }

  Assignment *ParseAssignmentRhs(Identifier *id) {
    // '=' already consumed
    return new Assignment(id, ParseExpression());
  }

  FunCall *ParseFunCall(Token identifier) {
    // opening '(' already consumed.
    List *args = ParseList(List::Type::kTuple,
                           [&]() { return ValueOrAssignment(); },
                           TokenType::kCloseParen);
    return new FunCall(new Identifier(identifier.text), args);
  }

  List *ParseList(List::Type type, const std::function<Node *()> &element_parse,
                  TokenType end_tok) {
    List *result = new List(type);
    Token upcoming = scanner_->Peek();
    while (upcoming.type != end_tok) {
      result->Append(element_parse());
      upcoming = scanner_->Peek();
      if (upcoming.type == ',') {
        scanner_->Next();
        upcoming = scanner_->Peek();
      } else if (upcoming.type != end_tok) {
        MsgAt(upcoming) << "Expected comma or close " << end_tok << "\n";
        SetErrorToken(scanner_->Next());
        return result;
      }
    }
    scanner_->Next();  // eats end_tok
    return result;
  }

  Node *ValueOrAssignment() {
    Node *value = ParseValue();
    if (value->is_identifier() && scanner_->Peek().type == '=') {
      scanner_->Next();
      return ParseAssignmentRhs(static_cast<Identifier*>(value));
    }
    return value;
  }

  IntScalar *ParseIntFromToken(Token t) {
    int64_t val = 0;
    auto result = std::from_chars(t.text.begin(), t.text.end(), val);
    if (result.ec != std::errc{}) {
      MsgAt(t) << "Can't parse integer " << result.ptr << "\n";
      SetErrorToken(t);
      return nullptr;
    }
    return new IntScalar(val);
  }

  StringScalar *ParseStringScalarFromToken(Token t) {
    std::string_view literal = t.text.substr(1);
    literal.remove_suffix(1);
    return new StringScalar(literal);
  }

  Node *ParseValue() {
    Token t = scanner_->Next();
    switch (t.type) {
    case TokenType::kStringLiteral:
      return ParseStringScalarFromToken(t);
    case TokenType::kNumberLiteral:
      return ParseIntFromToken(t);
    case TokenType::kIdentifier:
      if (scanner_->Peek().type == '(') {
        scanner_->Next();
        return ParseFunCall(t);
      }
      return new Identifier(t.text);
    case TokenType::kOpenSquare:
      return ParseList(List::Type::kList, [&]() { return ParseExpression(); }, TokenType::kCloseSquare);
    case TokenType::kOpenBrace:
      return ParseList(List::Type::kMap, [&]() { return ParseMapTuple(); }, TokenType::kCloseBrace);
      default:
        MsgAt(t) << "Expected value of sorts\n";
        SetErrorToken(t);
        return nullptr;
    }
  }

  Node *ParseExpression() {
    Node *n;
    if (scanner_->Peek().type == '(') {
      n = ParseParenExpression();
    } else {
      n = ParseValue();
    }
    if (n == nullptr) return n;

    const Token upcoming = scanner_->Peek();
    if (upcoming.type == '+' || upcoming.type == '-') {
      Token op = scanner_->Next();
      return new BinOpNode(n, ParseExpression(), op.type);
    } else {
      return n;
    }
  }

  Node *ParseParenExpression() {
    Token p = scanner_->Next();
    assert(p.type == '(');  // We have only be called if this is true.
    Node *exp = ParseExpression();
    p = scanner_->Next();
    if (p.type != ')') {
      MsgAt(p) << "Expected close parenthesis\n";
      SetErrorToken(p);
    }
    return exp;
  }

  BinOpNode *ParseMapTuple() {
    Token p = scanner_->Next();
    Node *lhs;
    switch (p.type) {
    case kStringLiteral:
      lhs = ParseStringScalarFromToken(p);
      break;
    case kNumberLiteral:
      lhs = ParseIntFromToken(p);
      break;
    default:
      MsgAt(p) << "Expected literal in map key\n";
      SetErrorToken(p);
      return nullptr;
    }

    p = scanner_->Next();
    if (p.type != ':') {
      MsgAt(p) << "Expected ':' in map-tuple\n";
      SetErrorToken(p);
      return nullptr;
    }
    return new BinOpNode(lhs, ParseExpression(), ':');
  }

  std::ostream &MsgAt(Token t) {
    std::cerr << filename_ << ":" << scanner_->GetPos(t.text) << "'" << t.text
              << "' ";
    return std::cerr;
  }

  void SetErrorToken(Token t) {
    MsgAt(t) << "Got error\n";
    last_token_ = t;
    error_ = true;
  }

  // Error token or kEof
  Token lastToken() { return last_token_; }

private:
  const char *filename_;
  Scanner *const scanner_;
  bool error_ = false;
  Token last_token_;
};

std::optional<std::string> ReadFileToString(const char *filename) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is.good()) return std::nullopt;
  std::string result;
  char buffer[4096];
  for (;;) {
    is.read(buffer, sizeof(buffer));
    result.append(buffer, is.gcount());
    if (!is.good()) break;
  }
  return result;
}

static int usage(const char *prog) {
  fprintf(stderr, "Usage: %s <filename> [<filename>...]\n", prog);
  return 1;
}

int main(int argc, char *argv[]) {
  if (argc <= 1) return usage(argv[0]);
  int file_count = 0;
  int file_error_count = 0;

  for (int i = 1; i < argc; ++i) {
    const char *const filename = argv[i];
    std::optional<std::string> content = ReadFileToString(filename);
    if (!content.has_value()) {
      std::cerr << "Could not read " << filename << "\n";
      ++file_error_count;
      continue;
    }
    ++file_count;
    Scanner scanner(*content);
    Parser parser(filename, &scanner);
    List *const statements = parser.parse();
    if (statements) {
      std::cerr << "------- file " << filename << "\n";
      PrintVisitor printer(std::cerr);
      statements->Accept(&printer);
      std::cerr << "\n";
    }
    const Token last = parser.lastToken();
    if (last.type != kEof) {
      std::cout << filename << ":" << scanner.GetPos(last.text) <<
        ": FAILED AT '" << last.text << "' ----------------- \n";
      ++file_error_count;
    }
  }

  fprintf(stderr, "Scanned %d files; %d file with issues.\n",
          file_count, file_error_count);

  return file_error_count;
}
