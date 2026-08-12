"""Extract normalized source-compatibility declarations from C++ headers."""

from __future__ import annotations

import re
from hashlib import sha256
from pathlib import Path

from check_public_surface import strip_comments
from header_manifest import GENERATED_HEADER_SOURCES

TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
    r"@[A-Za-z_]\w*@|[A-Za-z_]\w*|0[xX][0-9A-Fa-f']+|"
    r"\d(?:[\w'.]*\w)?|::|->\*|->|\.\.\.|<=>|<<=|>>=|"
    r"==|!=|<=|>=|&&|\|\||\+\+|--|<<|>>|\+=|-=|\*=|/=|%=|"
    r"&=|\|=|\^=|##|\[\[|\]\]|[{}()\[\];,:<>=*&~!?+/%|^.#-]"
)
IDENTIFIER_RE = re.compile(r"^[A-Za-z_]\w*$")
TYPE_KEYWORDS = frozenset({"class", "struct", "union"})
ACCESS = frozenset({"public", "protected", "private"})
SKIPPED_NAMESPACES = frozenset({"detail", "internal"})


def _tokens(text: str) -> list[str]:
  return TOKEN_RE.findall(text)


def _normalize(tokens: list[str]) -> str:
  return " ".join(tokens)


def _qualified(scope: tuple[str, ...]) -> str:
  return "::".join(scope) if scope else "::"


def _macro_contracts(text: str) -> list[str]:
  contracts: list[str] = []
  conditions: list[str] = []
  lines = text.splitlines()
  index = 0
  while index < len(lines):
    logical = lines[index]
    while logical.rstrip().endswith("\\") and index + 1 < len(lines):
      index += 1
      logical = logical.rstrip()[:-1] + " " + lines[index].lstrip()
    match = re.match(
        r"^\s*#\s*define\s+(TESS_[A-Za-z_]\w*)(.*)$", logical
    )
    if match is not None and not match.group(1).endswith(("_H", "_H_")):
      clean, _ = strip_comments(logical, False)
      clean_match = re.match(
          r"^\s*#\s*define\s+(TESS_[A-Za-z_]\w*)(.*)$", clean
      )
      if clean_match is None:
        index += 1
        continue
      body = _normalize(
          _tokens(clean_match.group(1) + clean_match.group(2))
      )
      prefix = _condition_scope(conditions)
      contracts.append(f"macro {prefix}{match.group(1)}:{body}")
    directive_text, _ = strip_comments(logical, False)
    directive = _conditional_directive(directive_text)
    if directive is not None:
      kind, value = directive
      if kind in {"if", "ifdef", "ifndef"}:
        conditions.append(_condition_name(conditions, kind, value))
      elif kind in {"elif", "else"} and conditions:
        conditions[-1] = _condition_name(conditions, kind, value)
      elif kind == "endif" and conditions:
        conditions.pop()
    index += 1
  return contracts


def _conditional_directive(line: str) -> tuple[str, str] | None:
  match = re.match(
      r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", line
  )
  if match is None:
    return None
  return match.group(1), _normalize(_tokens(match.group(2)))


def _condition_name(
    parents: list[str], kind: str, value: str
) -> str:
  identity = "/".join([*parents, kind, value])
  digest = sha256(identity.encode("utf-8")).hexdigest()[:16]
  return f"__tess_pp_{digest}"


def _condition_scope(conditions: list[str]) -> str:
  return "::".join(conditions) + ("::" if conditions else "")


def _code_tokens(text: str) -> list[str]:
  clean: list[str] = []
  in_block_comment = False
  continuation = False
  conditions: list[str] = []
  for raw_line in text.splitlines():
    line, in_block_comment = strip_comments(raw_line, in_block_comment)
    stripped = line.lstrip()
    if continuation:
      continuation = line.rstrip().endswith("\\")
      continue
    if stripped.startswith("#"):
      directive = _conditional_directive(line)
      if directive is not None:
        kind, value = directive
        if kind in {"if", "ifdef", "ifndef"}:
          name = _condition_name(conditions, kind, value)
          conditions.append(name)
          clean.append(f"namespace {name} {{")
        elif kind in {"elif", "else"} and conditions:
          clean.append("}")
          name = _condition_name(conditions, kind, value)
          conditions[-1] = name
          clean.append(f"namespace {name} {{")
        elif kind == "endif" and conditions:
          conditions.pop()
          clean.append("}")
      continuation = line.rstrip().endswith("\\")
      continue
    continuation = False
    clean.append(line)
  return _tokens("\n".join(clean))


class _ContractParser:
  def __init__(self, tokens: list[str]) -> None:
    self.tokens = tokens
    self.contracts: list[str] = []

  def parse(self) -> list[str]:
    self._scope(0, (), "namespace", True)
    return sorted(set(self.contracts))

  def _scope(
      self,
      index: int,
      names: tuple[str, ...],
      kind: str,
      visible: bool,
      aggregate_possible: bool = True,
  ) -> tuple[int, bool]:
    pending: list[str] = []
    access = kind != "class"
    aggregate_eligible = kind in TYPE_KEYWORDS and aggregate_possible
    while index < len(self.tokens):
      token = self.tokens[index]
      if token == "}" and kind != "namespace-root":
        aggregate_eligible &= not self._breaks_aggregate(
            pending, names, access
        )
        self._record(pending, names, kind, visible and access)
        return index + 1, aggregate_eligible
      if (
          kind in TYPE_KEYWORDS
          and token == ":"
          and len(pending) == 1
          and pending[0] in ACCESS
      ):
        access = pending[0] == "public"
        pending.clear()
        index += 1
        continue
      if token == ";":
        aggregate_eligible &= not self._breaks_aggregate(
            pending, names, access
        )
        self._record(pending, names, kind, visible and access)
        pending.clear()
        index += 1
        continue
      if token != "{":
        pending.append(token)
        index += 1
        continue

      if self._has_unclosed_group(pending):
        braced, index = self._braced_initializer(index)
        pending.extend(braced)
        continue

      if self._is_namespace(pending):
        namespace = self._namespace_name(pending)
        namespace_parts = tuple(part for part in namespace.split("::") if part)
        child_visible = visible and access and not bool(
            set(namespace_parts) & SKIPPED_NAMESPACES
        )
        pending.clear()
        index, _ = self._scope(
            index + 1,
            names + namespace_parts,
            "namespace",
            child_visible,
        )
        continue

      enum_name = self._enum_name(pending)
      if enum_name is not None:
        declaration_visible = visible and access
        qualified = _qualified(names + (enum_name,))
        if declaration_visible:
          self.contracts.append(
              f"type {qualified}:{_normalize(pending)}"
          )
        pending.clear()
        index = self._enum_body(
            index + 1, names + (enum_name,), declaration_visible
        )
        continue

      type_kind, type_name = self._type(pending)
      if type_kind is not None and type_name is not None:
        declaration_visible = visible and access
        qualified = _qualified(names + (type_name,))
        aggregate_possible = ":" not in pending
        if declaration_visible:
          self.contracts.append(
              f"type {qualified}:{_normalize(pending)}"
          )
        pending.clear()
        index, child_is_aggregate = self._scope(
            index + 1,
            names + (type_name,),
            type_kind,
            declaration_visible,
            aggregate_possible,
        )
        if declaration_visible and child_is_aggregate:
          self.contracts.append(f"aggregate {qualified}")
        continue

      if self._is_function(pending):
        if self._starts_constructor_braced_initializer(pending):
          braced, index = self._braced_initializer(index)
          pending.extend(braced)
          continue
        aggregate_eligible &= not self._breaks_aggregate(
            pending, names, access
        )
        self._record(pending, names, kind, visible and access)
        pending.clear()
        index = self._skip_braces(index + 1)
        continue

      braced, index = self._braced_initializer(index)
      pending.extend(braced)
    self._record(pending, names, kind, visible and access)
    aggregate_eligible &= not self._breaks_aggregate(
        pending, names, access
    )
    return index, aggregate_eligible

  @classmethod
  def _breaks_aggregate(
      cls,
      tokens: list[str],
      names: tuple[str, ...],
      public: bool,
  ) -> bool:
    if not tokens or not names:
      return False
    declaration = cls._without_constructor_initializer(tokens, names)
    if cls._is_constructor(declaration, names[-1]):
      return True
    if "virtual" in declaration:
      return True
    if public:
      return False
    non_data = (
        cls._is_function(declaration)
        or "using" in declaration
        or "typedef" in declaration
        or "friend" in declaration
        or "static" in declaration
        or "concept" in declaration
        or "enum" in declaration
        or any(keyword in declaration for keyword in TYPE_KEYWORDS)
    )
    return not non_data

  @staticmethod
  def _is_constructor(tokens: list[str], type_name: str) -> bool:
    if "(" not in tokens:
      return False
    opening = tokens.index("(")
    return opening > 0 and tokens[opening - 1] == type_name

  def _record(
      self,
      tokens: list[str],
      names: tuple[str, ...],
      scope_kind: str,
      visible: bool,
  ) -> None:
    if not tokens or not visible or tokens[0] == "static_assert":
      return
    declaration = self._without_constructor_initializer(tokens, names)
    normalized = _normalize(declaration)
    scope = _qualified(names)
    if scope_kind in TYPE_KEYWORDS:
      additive_member = (
          self._is_function(declaration)
          or "using" in declaration
          or "typedef" in declaration
          or "friend" in declaration
          or "static" in declaration
          or "concept" in declaration
      )
      category = "member" if additive_member else "data-member"
    elif self._is_function(tokens):
      category = "function"
    else:
      category = "declaration"
    self.contracts.append(f"{category} {scope}:{normalized}")

  @staticmethod
  def _without_constructor_initializer(
      tokens: list[str], names: tuple[str, ...]
  ) -> list[str]:
    if not names or "(" not in tokens:
      return tokens
    opening = tokens.index("(")
    if opening == 0 or tokens[opening - 1] != names[-1]:
      return tokens
    round_depth = 0
    function_closed = False
    for index in range(opening, len(tokens)):
      token = tokens[index]
      if token == "(":
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
        function_closed = round_depth == 0
      elif token == ":" and function_closed and round_depth == 0:
        return tokens[:index]
    return tokens

  @staticmethod
  def _has_unclosed_group(tokens: list[str]) -> bool:
    round_depth = 0
    square_depth = 0
    for token in tokens:
      if token == "(":
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
    return round_depth > 0 or square_depth > 0

  @staticmethod
  def _is_namespace(tokens: list[str]) -> bool:
    return "namespace" in tokens and "=" not in tokens

  @staticmethod
  def _namespace_name(tokens: list[str]) -> str:
    start = tokens.index("namespace") + 1
    parts = tokens[start:]
    return "".join(parts)

  @staticmethod
  def _type(tokens: list[str]) -> tuple[str | None, str | None]:
    positions: list[int] = []
    angle_depth = 0
    for index, token in enumerate(tokens):
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif token in TYPE_KEYWORDS and angle_depth == 0:
        positions.append(index)
    if not positions:
      return None, None
    position = positions[-1]
    for token in tokens[position + 1 :]:
      if IDENTIFIER_RE.fullmatch(token) and token not in {"final"}:
        return tokens[position], token
    return None, None

  @staticmethod
  def _enum_name(tokens: list[str]) -> str | None:
    if "enum" not in tokens:
      return None
    position = len(tokens) - 1 - tokens[::-1].index("enum")
    for token in tokens[position + 1 :]:
      if token in {"class", "struct"}:
        continue
      if IDENTIFIER_RE.fullmatch(token):
        return token
    return None

  @staticmethod
  def _is_function(tokens: list[str]) -> bool:
    if "(" not in tokens:
      return False
    if tokens and tokens[0] in {"using", "typedef"}:
      return False
    if "operator" in tokens:
      return True
    opening = _ContractParser._first_top_level_round(tokens)
    if opening is None:
      return False
    closing = _ContractParser._matching_round_bracket(tokens, opening)
    if closing is not None and closing + 1 < len(tokens):
      declarator = tokens[opening + 1 : closing]
      if (
          "(" not in declarator
          and any(token in {"*", "&", "&&"} for token in declarator)
          and tokens[closing + 1] == "("
      ):
        return False
    angle_depth = 0
    square_depth = 0
    for token in tokens[:opening]:
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      elif token == "=" and angle_depth == 0 and square_depth == 0:
        return False
    return True

  @staticmethod
  def _first_top_level_round(tokens: list[str]) -> int | None:
    angle_depth = 0
    square_depth = 0
    for index, token in enumerate(tokens):
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      elif token == "(" and angle_depth == 0 and square_depth == 0:
        return index
    return None

  @staticmethod
  def _matching_round_bracket(
      tokens: list[str], opening: int
  ) -> int | None:
    depth = 0
    for index in range(opening, len(tokens)):
      if tokens[index] == "(":
        depth += 1
      elif tokens[index] == ")":
        depth -= 1
        if depth == 0:
          return index
    return None

  @staticmethod
  def _starts_constructor_braced_initializer(tokens: list[str]) -> bool:
    round_depth = 0
    function_closed = False
    initializer_colon = False
    completed_initializer = False
    for token in tokens:
      if token == "(":
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
        if round_depth == 0:
          function_closed = True
      elif function_closed and round_depth == 0:
        if token == ":" and not initializer_colon:
          initializer_colon = True
        elif initializer_colon and token in {"}", ")"}:
          completed_initializer = True
        elif initializer_colon and token == ",":
          completed_initializer = False
    return initializer_colon and not completed_initializer

  def _skip_braces(self, index: int) -> int:
    depth = 1
    while index < len(self.tokens) and depth:
      if self.tokens[index] == "{":
        depth += 1
      elif self.tokens[index] == "}":
        depth -= 1
      index += 1
    return index

  def _braced_initializer(self, opening: int) -> tuple[list[str], int]:
    result = ["{"]
    depth = 1
    index = opening + 1
    while index < len(self.tokens) and depth:
      token = self.tokens[index]
      result.append(token)
      if token == "{":
        depth += 1
      elif token == "}":
        depth -= 1
      index += 1
    return result, index

  def _enum_body(
      self,
      index: int,
      names: tuple[str, ...],
      visible: bool,
  ) -> int:
    entry: list[str] = []
    round_depth = 0
    square_depth = 0
    brace_depth = 0
    ordinal = 0
    while index < len(self.tokens):
      token = self.tokens[index]
      if token == "(":
        round_depth += 1
      elif token == ")":
        round_depth -= 1
      elif token == "[":
        square_depth += 1
      elif token == "]":
        square_depth -= 1
      elif token == "{":
        brace_depth += 1
      elif token == "}" and brace_depth:
        brace_depth -= 1
      elif (
          token in {",", "}"}
          and round_depth == 0
          and square_depth == 0
          and brace_depth == 0
      ):
        if visible and entry:
          name = next(
              (item for item in entry if IDENTIFIER_RE.fullmatch(item)), "?"
          )
          self.contracts.append(
              f"enumerator {_qualified(names + (name,))}:"
              f"{_normalize(entry)} @index {ordinal}"
          )
          ordinal += 1
        entry.clear()
        index += 1
        if token == "}":
          return index
        continue
      entry.append(token)
      index += 1
    return index


def extract_api_contract(text: str) -> list[str]:
  """Return normalized declarations whose removal breaks source users."""
  parser = _ContractParser(_code_tokens(text))
  return sorted(set(_macro_contracts(text) + parser.parse()))


def current_api_contract(
    repo_root: Path, headers: list[str]
) -> dict[str, list[str]]:
  """Return declaration contracts keyed by installed header path."""
  result: dict[str, list[str]] = {}
  for header in headers:
    source = GENERATED_HEADER_SOURCES.get(header, header)
    result[header] = extract_api_contract(
        (repo_root / source).read_text(encoding="utf-8")
    )
  return result
