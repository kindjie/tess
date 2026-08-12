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
PP_END = "@tess_pp_end@"
PP_ELIF = "@tess_pp_elif@"
PP_ELSE = "@tess_pp_else@"
PARENTHESIZED_SPECIFIERS = frozenset(
    {
        "alignas",
        "decltype",
        "explicit",
        "noexcept",
        "requires",
        "sizeof",
        "typeid",
        "__attribute__",
        "__declspec",
    }
)
BUILTIN_TYPE_SPECIFIERS = frozenset(
    {
        "auto",
        "bool",
        "char",
        "char8_t",
        "char16_t",
        "char32_t",
        "double",
        "float",
        "int",
        "long",
        "short",
        "signed",
        "unsigned",
        "void",
        "wchar_t",
    }
)
DECLARATION_SPECIFIERS = BUILTIN_TYPE_SPECIFIERS | frozenset(
    {
        "const",
        "consteval",
        "constexpr",
        "constinit",
        "extern",
        "inline",
        "mutable",
        "register",
        "static",
        "thread_local",
        "typename",
        "volatile",
    }
)


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
    undefinition = re.match(
        r"^\s*#\s*undef\s+(TESS_[A-Za-z_]\w*)\b", logical
    )
    if undefinition is not None and not undefinition.group(1).endswith(
        ("_H", "_H_")
    ):
      name = undefinition.group(1)
      prefix = _condition_scope(conditions)
      contracts.append(f"macro-undef {prefix}{name}:{name}")
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


def callable_name(declaration: str | list[str]) -> str | None:
  """Return the declared callable name, skipping parenthesized specifiers."""
  tokens = _tokens(declaration) if isinstance(declaration, str) else declaration
  opening = _ContractParser._function_opening(tokens)
  if opening is None:
    return None
  prefix = _declarator_prefix(tokens[:opening])
  if "operator" in prefix:
    position = len(prefix) - 1 - prefix[::-1].index("operator")
    return "operator" + "".join(prefix[position + 1 :])
  positions = [
      index
      for index, token in enumerate(prefix)
      if IDENTIFIER_RE.fullmatch(token)
  ]
  if not positions:
    return None
  position = positions[-1]
  name = prefix[position]
  return f"~{name}" if position > 0 and prefix[position - 1] == "~" else name


def qualified_declares_type_name(tokens: list[str], name: str) -> bool:
  """Return whether a qualified type-id's terminal component is ``name``."""
  if tokens and IDENTIFIER_RE.fullmatch(tokens[-1]):
    return tokens[-1] == name
  qualified_templates: list[str] = []
  for index, token in enumerate(tokens[:-1]):
    if (
        not IDENTIFIER_RE.fullmatch(token)
        or tokens[index + 1] != "<"
    ):
      continue
    dependent = index >= 2 and tokens[index - 2 : index] == [
        "::",
        "template",
    ]
    qualified = index >= 1 and tokens[index - 1] == "::"
    separator = index - (2 if dependent else 1)
    follows_closed_template = (
        qualified
        and separator > 0
        and tokens[separator - 1] in {">", ">>"}
    )
    qualification_continues = "::" in tokens[index + 2 :]
    terminal_closes_singly = tokens[-1] == ">"
    if not dependent and not (
        follows_closed_template
        and (qualification_continues or terminal_closes_singly)
    ):
      continue
    qualified_templates.append(token)
  if qualified_templates and qualified_templates[-1] == name:
    return True
  terminal_template = _terminal_template_component(tokens)
  if terminal_template is not None:
    return terminal_template == name
  return any(
      token == name and index + 1 < len(tokens) and tokens[index + 1] == "<"
      for index, token in enumerate(tokens)
  )


def _terminal_template_component(tokens: list[str]) -> str | None:
  angle_depth = 0
  round_depth = 0
  square_depth = 0
  brace_depth = 0
  candidates: list[str] = []
  for index, token in enumerate(tokens):
    token = tokens[index]
    if token == "(":
      round_depth += 1
    elif token == ")" and round_depth:
      round_depth -= 1
    elif token in {"[", "[["}:
      square_depth += 1
    elif token in {"]", "]]"} and square_depth:
      square_depth -= 1
    elif token == "{":
      brace_depth += 1
    elif token == "}" and brace_depth:
      brace_depth -= 1
    elif not (round_depth or square_depth or brace_depth):
      if (
          angle_depth == 0
          and IDENTIFIER_RE.fullmatch(token)
          and index + 1 < len(tokens)
          and tokens[index + 1] == "<"
      ):
        candidates.append(token)
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
  return candidates[-1] if candidates else None


def _is_parenthesized_specifier(token: str) -> bool:
  return token in PARENTHESIZED_SPECIFIERS or token.startswith("TESS_")


def _declarator_prefix(tokens: list[str]) -> list[str]:
  result: list[str] = []
  index = 0
  while index < len(tokens):
    token = tokens[index]
    if token == "[[":
      depth = 1
      index += 1
      while index < len(tokens) and depth:
        if tokens[index] == "[[":
          depth += 1
        elif tokens[index] == "]]":
          depth -= 1
        index += 1
      continue
    if (
        _is_parenthesized_specifier(token)
        and index + 1 < len(tokens)
        and tokens[index + 1] == "("
    ):
      closing = _ContractParser._matching_round_bracket(tokens, index + 1)
      if closing is None:
        return result
      index = closing + 1
      continue
    if token.startswith("TESS_"):
      index += 1
      continue
    result.append(token)
    index += 1
  return result


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
          clean.append(f"@{name}@")
        elif kind in {"elif", "else"} and conditions:
          clean.append(PP_ELSE if kind == "else" else PP_ELIF)
          name = _condition_name(conditions, kind, value)
          conditions[-1] = name
          clean.append(f"@{name}@")
        elif kind == "endif" and conditions:
          conditions.pop()
          clean.append(PP_END)
      continuation = line.rstrip().endswith("\\")
      continue
    continuation = False
    clean.append(line)
  return _tokens("\n".join(clean))


class _ContractParser:
  def __init__(self, tokens: list[str]) -> None:
    self.tokens = tokens
    self.contracts: list[str] = []
    self.conditions: list[str] = []

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
    access = {kind != "class"}
    access_conditions: list[str] = []
    branch_states: list[dict[str, object]] = []
    expecting_branch = False
    aggregate_eligible = kind in TYPE_KEYWORDS and aggregate_possible
    while index < len(self.tokens):
      token = self.tokens[index]
      if token in {PP_ELIF, PP_ELSE}:
        if pending:
          pending.append(token)
        if self.conditions:
          self.conditions.pop()
        if branch_states:
          state = branch_states[-1]
          if access == {True}:
            state["public_conditions"].append(state["current_condition"])
          elif True in access:
            state["public_conditions"].extend(access_conditions)
          state["all_public"] = state["all_public"] and access == {True}
          state["combined"] = set(state["combined"]) | access
          access = set(state["entry"])
          if token == PP_ELSE:
            state["exhaustive"] = True
        expecting_branch = True
        index += 1
        continue
      if token == PP_END:
        if pending:
          pending.append(token)
        if self.conditions:
          self.conditions.pop()
        if branch_states:
          state = branch_states.pop()
          if access == {True}:
            state["public_conditions"].append(state["current_condition"])
          elif True in access:
            state["public_conditions"].extend(access_conditions)
          state["all_public"] = state["all_public"] and access == {True}
          combined = set(state["combined"]) | access
          if not state["exhaustive"]:
            combined |= set(state["entry"])
            state["all_public"] = (
                state["all_public"] and set(state["entry"]) == {True}
            )
          access = combined
          if state["changed"]:
            if state["all_public"]:
              access_conditions.clear()
            else:
              public_conditions = list(state["public_conditions"])
              if not state["exhaustive"] and set(state["entry"]) == {True}:
                identity = "/".join(state["conditions"])
                public_conditions.append(
                    f"@{_condition_name([], 'fallthrough', identity)}@"
                )
              access_conditions = list(dict.fromkeys(public_conditions))
        index += 1
        continue
      if token.startswith("@__tess_pp_"):
        if pending:
          pending.append(token)
        self.conditions.append(token)
        if expecting_branch and branch_states:
          branch_states[-1]["conditions"].append(token)
          branch_states[-1]["current_condition"] = token
          expecting_branch = False
        else:
          branch_states.append(
              {
                  "entry": set(access),
                  "combined": set(),
                  "changed": False,
                  "exhaustive": False,
                  "conditions": [token],
                  "current_condition": token,
                  "public_conditions": [],
                  "all_public": True,
              }
          )
        index += 1
        continue
      if token == "}" and kind != "namespace-root":
        aggregate_eligible = self._retain_aggregate(
            pending,
            names,
            access,
            access_conditions,
            aggregate_eligible,
        )
        self._record(
            pending,
            names,
            kind,
            visible and True in access,
            access_conditions,
        )
        return index + 1, aggregate_eligible
      if (
          kind in TYPE_KEYWORDS
          and token == ":"
          and len(pending) == 1
          and pending[0] in ACCESS
      ):
        next_access = {pending[0] == "public"}
        if branch_states and next_access != access:
          for state in branch_states:
            state["changed"] = True
        elif not branch_states:
          access_conditions.clear()
        access = next_access
        pending.clear()
        index += 1
        continue
      if token == ";":
        aggregate_eligible = self._retain_aggregate(
            pending,
            names,
            access,
            access_conditions,
            aggregate_eligible,
        )
        self._record(
            pending,
            names,
            kind,
            visible and True in access,
            access_conditions,
        )
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

      if self._starts_requires_expression(pending):
        braced, index = self._braced_initializer(index)
        pending.extend(braced)
        continue

      if self._is_namespace(pending):
        namespace = self._namespace_name(pending)
        namespace_parts = tuple(part for part in namespace.split("::") if part)
        child_visible = visible and True in access and not bool(
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

      if self._is_function(
          pending, names[-1] if kind in TYPE_KEYWORDS and names else None
      ):
        if self._starts_constructor_braced_initializer(pending):
          braced, index = self._braced_initializer(index)
          pending.extend(braced)
          continue
        aggregate_eligible = self._retain_aggregate(
            pending,
            names,
            access,
            access_conditions,
            aggregate_eligible,
        )
        self._record(
            pending,
            names,
            kind,
            visible and True in access,
            access_conditions,
        )
        pending.clear()
        index = self._skip_braces(index + 1)
        continue

      enum_name = self._enum_name(pending)
      if enum_name is not None:
        declaration_visible = visible and True in access
        qualified = _qualified(names + (enum_name,))
        if declaration_visible:
          self.contracts.append(
              f"type {qualified}:"
              f"{self._condition_prefix(access_conditions)}"
              f"{_normalize(pending)}"
          )
        pending.clear()
        index = self._enum_body(
            index + 1, names + (enum_name,), declaration_visible
        )
        continue

      type_kind, type_name = self._type(pending)
      if type_kind is not None and type_name is not None:
        declaration_visible = visible and True in access
        qualified = _qualified(names + (type_name,))
        aggregate_possible, base_break_conditions = (
            self._aggregate_bases_possible(pending, type_kind)
        )
        if declaration_visible:
          self.contracts.append(
              f"type {qualified}:"
              f"{self._condition_prefix(access_conditions)}"
              f"{_normalize(pending)}"
          )
          if aggregate_possible:
            self.contracts.extend(
                f"aggregate-break {qualified}:{condition}"
                for condition in base_break_conditions
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
          condition = self._condition_prefix(access_conditions).rstrip()
          suffix = f":{condition}" if condition else ""
          self.contracts.append(f"aggregate {qualified}{suffix}")
        continue

      braced, index = self._braced_initializer(index)
      pending.extend(braced)
    self._record(
        pending,
        names,
        kind,
        visible and True in access,
        access_conditions,
    )
    aggregate_eligible = self._retain_aggregate(
        pending,
        names,
        access,
        access_conditions,
        aggregate_eligible,
    )
    return index, aggregate_eligible

  def _retain_aggregate(
      self,
      tokens: list[str],
      names: tuple[str, ...],
      access: set[bool],
      access_conditions: list[str],
      eligible: bool,
  ) -> bool:
    if not eligible or not self._breaks_aggregate(
        tokens, names, access == {True}
    ):
      return eligible
    conditional = bool(self.conditions or access_conditions) or len(access) > 1
    if not conditional:
      return False
    declaration = self._without_constructor_initializer(tokens, names)
    access_independent = (
        self._is_constructor(declaration, names[-1])
        or self._is_inherited_constructor(declaration)
        or "virtual" in declaration
    )
    condition = self._condition_prefix(access_conditions).strip()
    if len(access) > 1 and not access_independent:
      condition = f"@private-access@ {condition}".strip()
    self.contracts.append(
        f"aggregate-break {_qualified(names)}:"
        f"{condition or '@conditional-access@'}"
    )
    return True

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
    if cls._is_inherited_constructor(declaration):
      return True
    if "virtual" in declaration:
      return True
    if public:
      return False
    non_data = (
        cls._is_function(declaration, names[-1])
        or "using" in declaration
        or "typedef" in declaration
        or "friend" in declaration
        or "static" in declaration
        or "concept" in declaration
        or cls._is_type_only_declaration(declaration)
    )
    return not non_data

  @staticmethod
  def _is_type_only_declaration(tokens: list[str]) -> bool:
    meaningful = _declarator_prefix(
        [
            token
            for token in tokens
            if not token.startswith("@__tess_pp_")
            and token not in {PP_END, PP_ELIF, PP_ELSE}
        ]
    )
    brace_depth = 0
    position = None
    for index, token in enumerate(meaningful):
      if token == "{":
        brace_depth += 1
      elif token == "}" and brace_depth:
        brace_depth -= 1
      elif (
          brace_depth == 0
          and position is None
          and (token == "enum" or token in TYPE_KEYWORDS)
      ):
        position = index
    if position is None:
      return False
    keyword = meaningful[position]
    opening = next(
        (
            index
            for index in range(position + 1, len(meaningful))
            if meaningful[index] == "{"
        ),
        None,
    )
    if opening is not None:
      depth = 0
      closing = None
      for index in range(opening, len(meaningful)):
        if meaningful[index] == "{":
          depth += 1
        elif meaningful[index] == "}":
          depth -= 1
          if depth == 0:
            closing = index
            break
      if closing is None or meaningful[closing + 1 :]:
        return False
      if keyword == "enum":
        return True
      return any(
          IDENTIFIER_RE.fullmatch(token) and token != "final"
          for token in meaningful[position + 1 : opening]
      )

    tail = meaningful[position + 1 :]
    if keyword == "enum":
      tail = [token for token in tail if token not in {"class", "struct"}]
      name = next(
          (
              index
              for index, token in enumerate(tail)
              if IDENTIFIER_RE.fullmatch(token)
          ),
          None,
      )
      if name is None:
        return False
      remainder = tail[name + 1 :]
      return not remainder or remainder[0] == ":"
    identifiers = [
        token
        for token in tail
        if IDENTIFIER_RE.fullmatch(token) and token != "final"
    ]
    return len(identifiers) == 1

  @staticmethod
  def _is_constructor(tokens: list[str], type_name: str) -> bool:
    return _ContractParser._constructor_opening(tokens, type_name) is not None

  @staticmethod
  def _constructor_opening(
      tokens: list[str], type_name: str
  ) -> int | None:
    opening = _ContractParser._function_opening(tokens)
    name = callable_name(tokens)
    if opening is None or name != type_name:
      return None
    prefix = [
        token
        for token in _declarator_prefix(tokens[:opening])
        if token not in DECLARATION_SPECIFIERS
    ]
    return opening if prefix == [type_name] else None

  @staticmethod
  def _is_inherited_constructor(tokens: list[str]) -> bool:
    if not tokens or tokens[0] != "using" or "=" in tokens:
      return False
    if "::" not in tokens:
      return False
    separator = len(tokens) - 1 - tokens[::-1].index("::")
    right = next(
        (
            token
            for token in tokens[separator + 1 :]
            if IDENTIFIER_RE.fullmatch(token)
        ),
        None,
    )
    return right is not None and qualified_declares_type_name(
        tokens[1:separator], right
    )

  @classmethod
  def _aggregate_bases_possible(
      cls, tokens: list[str], type_kind: str
  ) -> tuple[bool, list[str]]:
    variants = cls._conditional_variants(tokens)
    results = [
        (cls._aggregate_base_variant_possible(value, type_kind), conditions)
        for value, conditions in variants
    ]
    possible = any(result for result, _ in results)
    breaks = sorted(
        {
            _normalize(conditions)
            for result, conditions in results
            if not result and conditions
        }
    )
    return possible, breaks

  @staticmethod
  def _aggregate_base_variant_possible(
      tokens: list[str], type_kind: str
  ) -> bool:
    if ":" not in tokens:
      return True
    bases = tokens[tokens.index(":") + 1 :]
    segments: list[list[str]] = [[]]
    angle_depth = 0
    round_depth = 0
    square_depth = 0
    for token in bases:
      if token == "<":
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif token == "(":
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      if (
          token == ","
          and angle_depth == 0
          and round_depth == 0
          and square_depth == 0
      ):
        segments.append([])
      else:
        segments[-1].append(token)
    for segment in segments:
      declarations = [
          token
          for token in segment
          if not token.startswith("@__tess_pp_")
          and token not in {PP_END, PP_ELIF, PP_ELSE}
      ]
      if not declarations:
        continue
      if any(
          token in {"private", "protected", "virtual"}
          for token in declarations
      ):
        return False
      if type_kind == "class" and "public" not in declarations:
        return False
    return True

  @classmethod
  def _conditional_variants(
      cls, tokens: list[str]
  ) -> list[tuple[list[str], list[str]]]:
    opening = next(
        (
            index
            for index, token in enumerate(tokens)
            if token.startswith("@__tess_pp_")
        ),
        None,
    )
    if opening is None:
      return [(tokens, [])]

    branches: list[tuple[str, list[str]]] = []
    condition = tokens[opening]
    start = opening + 1
    depth = 1
    exhaustive = False
    index = start
    closing = None
    while index < len(tokens):
      token = tokens[index]
      if token.startswith("@__tess_pp_"):
        depth += 1
      elif token in {PP_ELIF, PP_ELSE}:
        if depth == 1:
          branches.append((condition, tokens[start:index]))
          exhaustive |= token == PP_ELSE
          index += 1
          if index >= len(tokens) or not tokens[index].startswith(
              "@__tess_pp_"
          ):
            return [(tokens, [])]
          condition = tokens[index]
          start = index + 1
        elif index + 1 < len(tokens) and tokens[index + 1].startswith(
            "@__tess_pp_"
        ):
          index += 1
      elif token == PP_END:
        depth -= 1
        if depth == 0:
          branches.append((condition, tokens[start:index]))
          closing = index
          break
      index += 1
    if closing is None:
      return [(tokens, [])]
    if not exhaustive:
      identity = "/".join(value for value, _ in branches)
      fallthrough = f"@{_condition_name([], 'fallthrough', identity)}@"
      branches.append((fallthrough, []))

    prefix = tokens[:opening]
    suffix = tokens[closing + 1 :]
    variants: list[tuple[list[str], list[str]]] = []
    for branch_condition, branch in branches:
      for value, conditions in cls._conditional_variants(
          prefix + branch + suffix
      ):
        path = [branch_condition] if branch_condition else []
        variants.append((value, path + conditions))
    return variants

  def _condition_prefix(self, extra: list[str] | None = None) -> str:
    conditions = list(self.conditions)
    if extra is not None:
      conditions.extend(extra)
    conditions = list(dict.fromkeys(conditions))
    return _normalize(conditions) + (" " if conditions else "")

  def _record(
      self,
      tokens: list[str],
      names: tuple[str, ...],
      scope_kind: str,
      visible: bool,
      conditions: list[str],
  ) -> None:
    if not tokens or not visible or tokens[0] == "static_assert":
      return
    declaration = self._without_constructor_initializer(tokens, names)
    normalized = self._condition_prefix(conditions) + _normalize(declaration)
    scope = _qualified(names)
    if scope_kind in TYPE_KEYWORDS:
      additive_member = (
          self._is_function(declaration, names[-1] if names else None)
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
    if not names:
      return tokens
    opening = _ContractParser._constructor_opening(tokens, names[-1])
    if opening is None:
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
    round_depth = 0
    square_depth = 0
    for index, token in enumerate(tokens):
      if token == "(":
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      elif token == "<" and round_depth == 0:
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif (
          token in TYPE_KEYWORDS
          and angle_depth == 0
          and round_depth == 0
          and square_depth == 0
      ):
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
  def _is_function(
      tokens: list[str], type_name: str | None = None
  ) -> bool:
    if "(" not in tokens:
      return False
    if tokens and tokens[0] in {"using", "typedef"}:
      return False
    if "operator" in tokens:
      return True
    opening = _ContractParser._function_opening(tokens)
    if opening is None:
      return False
    name = callable_name(tokens)
    if name is None and any(
        token == "enum" or token in TYPE_KEYWORDS
        for token in tokens[opening + 1 :]
    ):
      return False
    closing = _ContractParser._matching_round_bracket(tokens, opening)
    if closing is not None:
      declarator = tokens[opening + 1 : closing]
      named_function = _ContractParser._has_named_function_declarator(
          tokens, opening, name, type_name
      )
      if not named_function and (
          _ContractParser._is_parenthesized_object_declarator(
              tokens, opening, closing, type_name
          )
      ):
        return False
    if closing is not None and closing + 1 < len(tokens):
      declarator = tokens[opening + 1 : closing]
      if (
          "(" not in declarator
          and any(token in {"*", "&", "&&"} for token in declarator)
          and tokens[closing + 1] == "("
      ):
        return False
    angle_depth = 0
    round_depth = 0
    square_depth = 0
    for token in tokens[:opening]:
      if token == "(" and angle_depth:
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token == "<" and round_depth == 0:
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
  def _has_named_function_declarator(
      tokens: list[str],
      opening: int,
      name: str | None,
      type_name: str | None,
  ) -> bool:
    if name is None or opening == 0:
      return False
    grouped = _ContractParser._grouped_declarator_name(tokens, opening)
    if grouped is not None:
      return grouped == name
    plain_name = name.removeprefix("~")
    if tokens[opening - 1] != plain_name:
      return False
    if type_name is not None and plain_name == type_name:
      return not _declarator_prefix(tokens[: opening - 1])
    if name in DECLARATION_SPECIFIERS:
      return False
    prefix = _declarator_prefix(tokens[: opening - 1])
    if not prefix or prefix[-1] == "::":
      return False
    return any(token not in DECLARATION_SPECIFIERS for token in prefix)

  @staticmethod
  def _grouped_declarator_name(
      tokens: list[str], parameter_opening: int
  ) -> str | None:
    if parameter_opening == 0 or tokens[parameter_opening - 1] != ")":
      return None
    depth = 0
    grouping = None
    for index in range(parameter_opening - 1, -1, -1):
      if tokens[index] == ")":
        depth += 1
      elif tokens[index] == "(":
        depth -= 1
        if depth == 0:
          grouping = index
          break
    if grouping is None:
      return None
    declarator = _ContractParser._normalize_grouped_declarator(
        tokens[grouping + 1 : parameter_opening - 1]
    )
    if len(declarator) != 1:
      return None
    return (
        declarator[0]
        if IDENTIFIER_RE.fullmatch(declarator[0])
        else None
    )

  @staticmethod
  def _normalize_grouped_declarator(tokens: list[str]) -> list[str]:
    declarator = _declarator_prefix(tokens)
    while (
        len(declarator) >= 2
        and declarator[0] == "("
        and _ContractParser._matching_round_bracket(declarator, 0)
        == len(declarator) - 1
    ):
      declarator = _declarator_prefix(declarator[1:-1])
    return declarator

  @staticmethod
  def _is_parenthesized_object_declarator(
      tokens: list[str],
      opening: int,
      closing: int,
      type_name: str | None,
  ) -> bool:
    prefix = _declarator_prefix(tokens[:opening])
    identifiers: list[tuple[int, str]] = []
    builtin_type = False
    angle_depth = 0
    round_depth = 0
    for index, token in enumerate(prefix):
      if token == "(" and angle_depth:
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token == "<" and round_depth == 0:
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif angle_depth == 0 and round_depth == 0:
        if token in BUILTIN_TYPE_SPECIFIERS:
          builtin_type = True
        elif (
            IDENTIFIER_RE.fullmatch(token)
            and token not in DECLARATION_SPECIFIERS
        ):
          identifiers.append((index, token))
    if type_name is not None and identifiers:
      owner_index, owner_name = identifiers[-1]
      qualified_owner = (
          owner_index > 0 and prefix[owner_index - 1] == "::"
      )
      member_pointer_owner = (
          owner_index + 1 < len(prefix)
          and prefix[owner_index + 1] == "::"
      )
      if (
          owner_name == type_name
          and not qualified_owner
          and not member_pointer_owner
      ):
        return False
    if any(
        prefix[index : index + 2] == ["::", "*"]
        for index in range(len(prefix) - 1)
    ):
      identifiers = []
    if builtin_type:
      if identifiers:
        return False
    elif len(identifiers) > 1:
      if not all(
          prefix[right_index - 1] == "::"
          for (left_index, _), (right_index, _) in zip(
              identifiers, identifiers[1:]
          )
          if right_index > left_index
      ):
        return False

    declarator = tokens[opening + 1 : closing]
    while (
        len(declarator) >= 2
        and declarator[0] == "("
        and _ContractParser._matching_round_bracket(declarator, 0)
        == len(declarator) - 1
    ):
      declarator = declarator[1:-1]
    declarator = _ContractParser._strip_declarator_annotations(declarator)
    if not declarator:
      return False
    pointer_or_reference = any(
        token in {"*", "&", "&&"} for token in declarator
    )
    names = [
        token for token in declarator if IDENTIFIER_RE.fullmatch(token)
    ]
    if pointer_or_reference:
      if _ContractParser._function_pointer_return_opening(
          tokens, opening, closing
      ) is not None:
        return False
      return bool(names)
    if "[" in declarator and names:
      return True
    if len(declarator) != 1 or len(names) != 1:
      return False
    return closing + 1 >= len(tokens) or tokens[closing + 1] != "("

  @staticmethod
  def _strip_declarator_annotations(tokens: list[str]) -> list[str]:
    result: list[str] = []
    index = 0
    while index < len(tokens):
      token = tokens[index]
      if token == "[[":
        depth = 1
        index += 1
        while index < len(tokens) and depth:
          if tokens[index] == "[[":
            depth += 1
          elif tokens[index] == "]]":
            depth -= 1
          index += 1
        continue
      if token in {"__attribute__", "__declspec"} or token.startswith(
          "TESS_"
      ):
        index += 1
        if index < len(tokens) and tokens[index] == "(":
          closing = _ContractParser._matching_round_bracket(tokens, index)
          if closing is None:
            return result
          index = closing + 1
        continue
      result.append(token)
      index += 1
    return result

  @staticmethod
  def _function_opening(tokens: list[str]) -> int | None:
    start = 0
    while start < len(tokens):
      opening = _ContractParser._first_top_level_round(tokens, start)
      if opening is None:
        return _ContractParser._fallback_function_opening(tokens, start)
      closing = _ContractParser._matching_round_bracket(tokens, opening)
      if closing is None:
        return None
      pointer_return = _ContractParser._function_pointer_return_opening(
          tokens, opening, closing
      )
      if pointer_return is not None:
        return pointer_return
      grouped_name = _ContractParser._normalize_grouped_declarator(
          tokens[opening + 1 : closing]
      )
      if (
          len(grouped_name) == 1
          and IDENTIFIER_RE.fullmatch(grouped_name[0])
          and closing + 1 < len(tokens)
          and tokens[closing + 1] == "("
          and (opening == 0 or not tokens[opening - 1].startswith("TESS_"))
      ):
        return closing + 1
      previous = tokens[opening - 1] if opening else ""
      if previous.startswith("TESS_"):
        if _ContractParser._first_top_level_round(tokens, closing + 1) is None:
          return opening
      elif previous not in PARENTHESIZED_SPECIFIERS:
        return opening
      start = closing + 1
    return None

  @staticmethod
  def _function_pointer_return_opening(
      tokens: list[str], opening: int, closing: int
  ) -> int | None:
    pointer_seen = False
    square_depth = 0
    ignored_until = -1
    for index in range(opening + 1, closing):
      if index <= ignored_until:
        continue
      token = tokens[index]
      if token in {"[", "[["}:
        square_depth += 1
        continue
      if token in {"]", "]]"} and square_depth:
        square_depth -= 1
        continue
      if square_depth:
        continue
      if token in {"__attribute__", "__declspec"} or token.startswith(
          "TESS_"
      ):
        if index + 1 < closing and tokens[index + 1] == "(":
          annotation_end = _ContractParser._matching_round_bracket(
              tokens, index + 1
          )
          if annotation_end is not None and annotation_end < closing:
            ignored_until = annotation_end
        continue
      if token in {"*", "&", "&&"}:
        pointer_seen = True
      elif (
          pointer_seen
          and token == "("
          and not _ContractParser._is_grouped_name_before_parameters(
              tokens, index, closing
          )
          and _ContractParser._grouped_declarator_name(tokens, index)
          is not None
      ):
        return index
      elif (
          pointer_seen
          and IDENTIFIER_RE.fullmatch(token)
          and token not in PARENTHESIZED_SPECIFIERS
          and not token.startswith("TESS_")
          and index + 1 < closing
          and tokens[index + 1] == "("
      ):
        return index + 1
    return None

  @staticmethod
  def _is_grouped_name_before_parameters(
      tokens: list[str], opening: int, limit: int
  ) -> bool:
    closing = _ContractParser._matching_round_bracket(tokens, opening)
    if closing is None or closing >= limit:
      return False
    grouped = _ContractParser._normalize_grouped_declarator(
        tokens[opening + 1 : closing]
    )
    return (
        len(grouped) == 1
        and IDENTIFIER_RE.fullmatch(grouped[0]) is not None
        and closing + 1 < limit
        and tokens[closing + 1] == "("
    )

  @staticmethod
  def _fallback_function_opening(
      tokens: list[str], start: int = 0
  ) -> int | None:
    candidates: list[int] = []
    round_depth = 0
    square_depth = 0
    brace_depth = 0
    for index, token in enumerate(tokens):
      if token == "(":
        if (
            index >= start
            and round_depth == 0
            and square_depth == 0
            and brace_depth == 0
        ):
          candidates.append(index)
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      elif token == "{":
        brace_depth += 1
      elif token == "}" and brace_depth:
        brace_depth -= 1
    for opening in candidates:
      if opening == 0:
        continue
      previous = tokens[opening - 1]
      closing = _ContractParser._matching_round_bracket(tokens, opening)
      if closing is None:
        continue
      grouped = _ContractParser._normalize_grouped_declarator(
          tokens[opening + 1 : closing]
      )
      if (
          len(grouped) == 1
          and IDENTIFIER_RE.fullmatch(grouped[0])
          and closing + 1 < len(tokens)
          and tokens[closing + 1] == "("
      ):
        continue
      grouped_name = _ContractParser._grouped_declarator_name(tokens, opening)
      if (
          grouped_name is None
          and not IDENTIFIER_RE.fullmatch(previous)
          or previous in PARENTHESIZED_SPECIFIERS
          or previous.startswith("TESS_")
      ):
        continue
      suffix = tokens[closing + 1 :]
      suffix_starts_function_tail = suffix and (
          suffix[0]
          in {
              "&",
              "&&",
              "->",
              "=",
              "[[",
              "const",
              "constexpr",
              "final",
              "noexcept",
              "override",
              "requires",
              "volatile",
          }
          or suffix[0].startswith("TESS_")
      )
      if (
          suffix
          and not suffix_starts_function_tail
          and any(IDENTIFIER_RE.fullmatch(token) for token in suffix)
      ):
        continue
      return opening
    return None

  @staticmethod
  def _first_top_level_round(
      tokens: list[str], start: int = 0
  ) -> int | None:
    angle_depth = 0
    round_depth = 0
    square_depth = 0
    for index, token in enumerate(tokens):
      if token == "(" and angle_depth:
        round_depth += 1
      elif token == ")" and round_depth:
        round_depth -= 1
      elif token == "<" and round_depth == 0 and not (
          index > 0 and tokens[index - 1] == "operator"
      ):
        angle_depth += 1
      elif token == ">" and angle_depth:
        angle_depth -= 1
      elif token == ">>" and angle_depth:
        angle_depth = max(0, angle_depth - 2)
      elif token in {"[", "[["}:
        square_depth += 1
      elif token in {"]", "]]"} and square_depth:
        square_depth -= 1
      elif (
          index >= start
          and token == "("
          and angle_depth == 0
          and round_depth == 0
          and square_depth == 0
      ):
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

  @staticmethod
  def _starts_requires_expression(tokens: list[str]) -> bool:
    opening = _ContractParser._function_opening(tokens)
    if opening is None:
      return False
    closing = _ContractParser._matching_round_bracket(tokens, opening)
    if closing is None:
      return False
    suffix = tokens[closing + 1 :]
    positions = [
        index for index, token in enumerate(suffix) if token == "requires"
    ]
    if not positions:
      return False
    position = positions[-1]
    if position + 1 == len(suffix):
      return True
    if suffix[position + 1] != "(":
      return False
    requirement_closing = _ContractParser._matching_round_bracket(
        suffix, position + 1
    )
    return requirement_closing == len(suffix) - 1

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
    entry_conditions: list[str] = []
    round_depth = 0
    square_depth = 0
    brace_depth = 0
    ordinals = {0}
    ordinal_branches: list[dict[str, object]] = []
    expecting_branch = False

    def finish_entry() -> None:
      nonlocal ordinals
      if not entry:
        entry_conditions.clear()
        return
      if visible:
        name = next(
            (item for item in entry if IDENTIFIER_RE.fullmatch(item)), "?"
        )
        self.contracts.append(
            f"enumerator {_qualified(names + (name,))}:"
            f"{self._condition_prefix(entry_conditions)}"
            f"{_normalize(entry)} "
            f"@index {','.join(str(value) for value in sorted(ordinals))}"
        )
      ordinals = {value + 1 for value in ordinals}
      entry.clear()
      entry_conditions.clear()

    while index < len(self.tokens):
      token = self.tokens[index]
      if token in {PP_ELIF, PP_ELSE}:
        finish_entry()
        if self.conditions:
          self.conditions.pop()
        if ordinal_branches:
          state = ordinal_branches[-1]
          state["combined"].update(ordinals)
          ordinals = set(state["entry"])
          if token == PP_ELSE:
            state["exhaustive"] = True
        expecting_branch = True
        index += 1
        continue
      if token == PP_END:
        finish_entry()
        if self.conditions:
          self.conditions.pop()
        if ordinal_branches:
          state = ordinal_branches.pop()
          ordinals.update(state["combined"])
          if not state["exhaustive"]:
            ordinals.update(state["entry"])
        index += 1
        continue
      if token.startswith("@__tess_pp_"):
        self.conditions.append(token)
        entry_conditions.append(token)
        if expecting_branch:
          expecting_branch = False
        else:
          ordinal_branches.append(
              {
                  "entry": set(ordinals),
                  "combined": set(),
                  "exhaustive": False,
              }
          )
        index += 1
        continue
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
        finish_entry()
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
