# Chapter 7: Syntax Highlighting

*A regex-based tokenizer with multi-line range support, incremental re-highlighting, and language definitions for ten syntaxes.*

---

## 7.1 — Architecture Overview

Syntax highlighting is the feature that transforms a wall of monochrome text into a readable, structured display of code. Keywords appear in one color, strings in another, comments in a muted tone, and function names in a distinct hue. The human eye can scan highlighted code far faster than plain text, because the color patterns create a visual structure that mirrors the syntactic structure of the program.

Our syntax highlighting system is split into three components, mirroring the architecture of *lite*. In *lite*, the system is divided into `core.syntax` (which stores language specifications), `core.tokenizer` (which converts text into tokens using a given syntax), and `core.doc.highlighter` (which caches tokenized state for each line and handles incremental re-highlighting). We follow the same separation:

**`SyntaxDef`** stores the specification for a language — its name, file extensions, and pattern rules. It is a passive data object with no behavior.

**`Tokenizer`** takes a line of text and a state object, and produces an array of tokens (each with `text` and `type` properties) and a new state object. It is stateless between calls — all the context it needs is provided through the `state` parameter.

**The highlight cache** (`hlCache` on the `Doc` class) stores the tokenization results for each line. It handles full-document tokenization at load time, incremental re-tokenization after edits, and lazy tokenization for lines that have not yet been processed.

This separation has a practical benefit. The syntax definitions are just data — adding a new language means adding a new `SyntaxDef` object to the `Syntaxes` array. The tokenizer is a generic engine that works with any syntax definition. And the cache is a performance optimization that is invisible to the rest of the editor — the rendering code calls `getTokensForLine(i)` and gets tokens, without knowing whether they were cached or freshly computed.

In *lite*, the tokenizer uses Lua patterns (similar to regular expressions but simpler). We use JavaScript regular expressions, which are more powerful but follow the same principle: patterns are matched against the input text from left to right, and the first match at the current position determines the token type.

The advantage of regex-based tokenization over a handwritten lexer is extensibility. Adding a new language does not require writing new code — it requires writing a new data definition. The `SyntaxDef` for Rust, for example, is just a list of regex patterns and their types. The tokenizer does not know or care that it is tokenizing Rust rather than Python; it just applies the patterns. This is the same approach that *lite* takes with Lua patterns — language syntax specifications are data files, not code, and they can be added, modified, or removed without changing the tokenizer itself.

The disadvantage is that regular expressions cannot express all syntactic constructs. Context-free constructs like balanced parentheses, nested templates, or indentation-sensitive block structure are beyond the reach of regular languages. Our tokenizer handles these limitations gracefully — it produces "good enough" highlighting that is correct for the common cases, even when it cannot perfectly analyze every edge case. This is a conscious design trade-off: perfect highlighting would require a full parser for each language, which would multiply the code size and complexity by an order of magnitude.


## 7.2 — Syntax Definitions

A syntax definition is a `SyntaxDef` object with three properties:

```javascript
class SyntaxDef {
  constructor(name, extensions, patterns) {
    this.name = name;
    this.extensions = extensions;
    this.patterns = patterns;
  }
}
```

The `name` is a human-readable string displayed in the status bar and the View menu — "JavaScript", "Python", "C/C++". The `extensions` is an array of file extensions — `[".js", ".jsx", ".mjs"]` — used for automatic syntax detection when a file is opened. The `patterns` is an array of pattern objects that define how the language's syntax is tokenized.

Each pattern object has one of two forms. A **single-line pattern** has a `regex` property and a `type`:

```javascript
{ regex: /\/\/.*/, type: "comment" }
```

This matches a line comment in JavaScript — `//` followed by anything until the end of the line. The `type` determines the color: `"comment"` maps to `Theme.comment`, a muted gray.

A **multi-line range pattern** has `start`, `end`, and `type` properties:

```javascript
{ start: /\/\*/, end: /\*\//, type: "comment" }
```

This matches a block comment in JavaScript — `/*` followed by anything (possibly spanning multiple lines) until `*/`. The range pattern is what allows comments and strings to span line boundaries.

The token types used across all syntax definitions are: `comment`, `string`, `number`, `keyword`, `keyword2`, `literal`, `func`, `symbol`, `operator`, and `normal`. Each maps to a color in the `Theme` object via the `tokenTypeToColor` function:

```javascript
function tokenTypeToColor(type) {
  return Theme[type] || Theme.normal;
}
```

The mapping is direct — the token type string is used as a key into the `Theme` object. If a type has no corresponding theme entry, it falls back to `Theme.normal`. This makes adding new token types trivial: define a pattern with a new type string and add a color entry to the theme.

The distinction between `keyword` and `keyword2` is a common pattern in syntax highlighting. `keyword` is used for language control-flow and structural keywords — `if`, `else`, `for`, `while`, `function`, `class`, `return`. `keyword2` is used for built-in types, standard library names, and other special identifiers — `this`, `window`, `console`, `Math`, `Object`. They are colored differently (yellow for keywords, purple for builtins in our theme) to help the reader distinguish language constructs from library features.

The `func` type is used for identifiers that appear immediately before an opening parenthesis — a heuristic for function calls. The pattern `[a-zA-Z_$][a-zA-Z0-9_$]*(?=\s*\()` uses a lookahead `(?=\s*\()` to match only when the identifier is followed by optional whitespace and an open paren. This colors function calls differently from plain variables, which improves readability.

**Pattern precedence** is determined by position in the array. The tokenizer tries patterns from top to bottom, and the first match at the current position wins. This ordering is critical. Consider the JavaScript patterns for identifiers:

```javascript
{ regex: /[a-zA-Z_$][a-zA-Z0-9_$]*(?=\s*\()/, type: "func" },
{ regex: /[a-zA-Z_$][a-zA-Z0-9_$]*/, type: "symbol" },
```

The function pattern must come before the symbol pattern. Both patterns match the same identifier text, but the function pattern has the additional lookahead requirement. If the symbol pattern came first, it would match every identifier — including function calls — and the function pattern would never get a chance to match. By placing the more specific pattern first, function calls get the `func` type, and plain identifiers fall through to the `symbol` type.

Similarly, comments must come before operators. The pattern for `//` comments would conflict with the operator pattern that matches `+`, `-`, `/`, etc. By placing comments first, `//` is recognized as a comment before the `/` could be matched as an operator.


## 7.3 — The Tokenizer

The `Tokenizer` class has one important method: `tokenize`. It takes a line of text and a state object, and returns an array of tokens and a new state:

```javascript
tokenize(lineText, state) {
  const tokens = [];
  let pos = 0;
  const patterns = this.syntax.patterns;

  // Phase 1: Handle continuation of a multi-line range
  if (state && state.rangeIdx !== null) {
    const pat = patterns[state.rangeIdx];
    const endMatch = lineText.match(pat.end);
    if (endMatch) {
      const endPos = endMatch.index + endMatch[0].length;
      tokens.push({
        text: lineText.substring(0, endPos), type: pat.type
      });
      pos = endPos;
      state = { rangeIdx: null };
    } else {
      tokens.push({ text: lineText, type: pat.type });
      return { tokens, state };
    }
  }

  if (!state) state = { rangeIdx: null };

  // Phase 2: Main tokenization loop
  while (pos < lineText.length) {
    let matched = false;
    const remaining = lineText.substring(pos);

    for (let p = 0; p < patterns.length; p++) {
      const pat = patterns[p];

      if (pat.start) {
        // Range pattern
        const startMatch = remaining.match(pat.start);
        if (startMatch && startMatch.index === 0) {
          const afterStart =
            lineText.substring(pos + startMatch[0].length);
          const endMatch = afterStart.match(pat.end);
          if (endMatch) {
            const fullLen = startMatch[0].length
              + endMatch.index + endMatch[0].length;
            tokens.push({
              text: lineText.substring(pos, pos + fullLen),
              type: pat.type
            });
            pos += fullLen;
          } else {
            tokens.push({
              text: lineText.substring(pos), type: pat.type
            });
            return { tokens, state: { rangeIdx: p } };
          }
          matched = true;
          break;
        }
      } else if (pat.regex) {
        // Single-line pattern
        const m = remaining.match(pat.regex);
        if (m && m.index === 0) {
          tokens.push({ text: m[0], type: pat.type });
          pos += m[0].length;
          matched = true;
          break;
        }
      }
    }

    // Phase 3: No pattern matched — accumulate as "normal"
    if (!matched) {
      const last = tokens.length > 0
        ? tokens[tokens.length - 1] : null;
      if (last && last.type === "normal") {
        last.text += lineText[pos];
      } else {
        tokens.push({ text: lineText[pos], type: "normal" });
      }
      pos++;
    }
  }

  if (tokens.length === 0) {
    tokens.push({ text: "", type: "normal" });
  }

  return { tokens, state };
}
```

The tokenizer operates in three phases.

**Phase 1: Range continuation.** If the state indicates we are inside a multi-line range (like a block comment that started on a previous line), we scan for the range's end pattern. If found, we push a token covering everything up to and including the end marker, advance `pos` past it, and reset the state. If not found, the entire line is inside the range — we push the whole line as a single token and return immediately, keeping the same range state for the next line.

**Phase 2: Pattern matching.** The main loop processes text from left to right. At each position, we extract the remaining text (`lineText.substring(pos)`) and try each pattern in order. For a single-line pattern, we call `remaining.match(pat.regex)` and check that the match starts at position 0 — this ensures we are matching at the current position, not somewhere later in the string. If it matches, we push a token and advance `pos` by the match length.

For a range pattern, we first check if the start pattern matches at position 0. If it does, we check if the end pattern appears later on the same line. If both start and end are found on the same line, the range is self-contained — we push a single token covering the entire range. If only the start is found, the range continues to the next line — we push a token covering the rest of the line and return with the range state set.

**Phase 3: Normal text accumulation.** If no pattern matches at the current position, the character is plain text. We accumulate consecutive unmatched characters into a single `"normal"` token by appending to the last token if it is already of type `"normal"`. This coalescing prevents a line of plain text from producing one token per character.

The tokenizer returns `{ tokens, state }`. The tokens are the result for this line. The state is carried to the next line — if we are inside a range, the state records which range, and the next line's tokenization will start in Phase 1. If we are not inside a range, the state is `{ rangeIdx: null }`.

Let us trace through a concrete example. The JavaScript line `var x = "hello"; // comment` with state `{ rangeIdx: null }`:

1. `pos = 0`, remaining = `var x = "hello"; // comment`
   - Pattern 0 (`//` comment): no match at position 0.
   - Pattern 1 (`/* */` block comment): no match.
   - Pattern 2 (template string): no match.
   - ...
   - Pattern 7 (keywords): `/\b(?:function|...|var|...)\b/` matches `"var"` at position 0.
   - Token: `{ text: "var", type: "keyword" }`. `pos = 3`.

2. `pos = 3`, remaining = ` x = "hello"; // comment`
   - No pattern matches a space. Normal accumulation: `{ text: " ", type: "normal" }`. `pos = 4`.

3. `pos = 4`, remaining = `x = "hello"; // comment`
   - Pattern 9 (func): `x` followed by lookahead for `(` — fails (followed by space).
   - Pattern 10 (symbol): `x` matches. Token: `{ text: "x", type: "symbol" }`. `pos = 5`.

4. `pos = 5`, remaining = ` = "hello"; // comment`
   - Space → normal. Appended to previous normal token? No, previous is `"symbol"`. New token: `{ text: " ", type: "normal" }`. `pos = 6`.

5. `pos = 6`, remaining = `= "hello"; // comment`
   - Pattern 11 (operators): `=` matches. Token: `{ text: "=", type: "operator" }`. `pos = 7`.

6. `pos = 7`, remaining = ` "hello"; // comment`
   - Space → normal. `pos = 8`.

7. `pos = 8`, remaining = `"hello"; // comment`
   - Pattern 3 (double-quoted string): `/"(?:[^"\\]|\\.)*"/` matches `"hello"`. Token: `{ text: '"hello"', type: "string" }`. `pos = 15`.

8. `pos = 15`, remaining = `; // comment`
   - `;` — no pattern matches (our JavaScript syntax does not have a specific pattern for semicolons). Normal: `{ text: ";", type: "normal" }`. `pos = 16`.

9. `pos = 16`, remaining = ` // comment`
   - Space → normal. Appended to previous normal (`;`): `{ text: "; ", type: "normal" }`. Wait — the previous token's text is `";"` which is type `"normal"`, so we append: `{ text: "; ", type: "normal" }`. `pos = 17`.

10. `pos = 17`, remaining = `// comment`
    - Pattern 0 (`//` comment): `/\/\/.*/` matches `"// comment"`. Token: `{ text: "// comment", type: "comment" }`. `pos = 27`.

11. `pos = 27`: end of line. Return tokens and state `{ rangeIdx: null }`.

The result is seven tokens: `"var"` (keyword), `" "` (normal), `"x"` (symbol), `" "` (normal), `"="` (operator), `" "` (normal), `'"hello"'` (string), `"; "` (normal), `"// comment"` (comment). Each is drawn in its corresponding theme color by the font atlas.

A critical detail in the main loop is the `startMatch.index === 0` check (for range patterns) and the `m.index === 0` check (for single-line patterns). JavaScript's `String.match` method searches for the pattern anywhere in the string, not just at the beginning. Without the `=== 0` check, a pattern like `/\/\/.*/` would match `//` wherever it appears in the remaining text — including inside a string like `"http://example.com"`. The position-0 check ensures we only match at the current scanning position, not further ahead.

This means the tokenizer advances one character at a time through text that does not match any pattern. If we have the text `;;;` and no pattern matches `;`, the tokenizer processes one character per iteration, accumulating them into a single `"normal"` token. The accumulation optimization — checking whether the last token is already `"normal"` and appending to it rather than creating a new token — reduces the token count for lines with many unmatched characters.

The empty-line edge case is handled at the end of the function:

```javascript
if (tokens.length === 0) {
  tokens.push({ text: "", type: "normal" });
}
```

An empty line has no characters to tokenize, so the main loop does not run and no tokens are produced. We push an empty `"normal"` token to ensure the return value always has at least one token. This simplifies the rendering code, which can assume every line has at least one token.

The `state` parameter defaults to `{ rangeIdx: null }` if not provided or if null. This means the first line of a file, and any line after a complete range, starts in the "outside all ranges" state. The state object is intentionally minimal — a single integer or null — because it is compared between lines using `JSON.stringify` in the incremental highlight optimization. A larger state object would make these comparisons slower, but for our simple state, the cost is negligible.


## 7.4 — Multi-Line Ranges

The most interesting aspect of the tokenizer is its handling of constructs that span multiple lines. A block comment like this:

```javascript
/* This is a
   multi-line
   comment */
```

spans three lines. The tokenizer must recognize that lines 2 and 3 are inside a comment even though they do not start with `/*`. This is where the `state` object and range patterns come into play.

*Lite* solves this problem identically. The highlighter in *lite* persists range state between lines by storing the pattern table index of the current range. When a line ends inside a range, the index is saved in the state. When the next line is tokenized, the tokenizer resumes from inside that range.

In our implementation, the state object is `{ rangeIdx: p }`, where `p` is the index of the range pattern in the pattern array. For the JavaScript block comment pattern at index 1 in the pattern list, a line that ends inside a block comment returns `state = { rangeIdx: 1 }`.

Let us trace through the three-line block comment. Assume the patterns list has `{ start: /\/\*/, end: /\*\//, type: "comment" }` at index 1.

**Line 1:** `code(); /* This is a`
- Tokenizer processes `code()` normally (function, parens, etc.).
- At `/*`, pattern 1's start matches. The tokenizer searches the rest of the line for `*/`. It is not found.
- Everything from `/*` to the end of the line is pushed as a `"comment"` token.
- The tokenizer returns `{ tokens: [..., { text: "/* This is a", type: "comment" }], state: { rangeIdx: 1 } }`.

**Line 2:** `   multi-line`
- State is `{ rangeIdx: 1 }`. Phase 1 runs.
- The tokenizer searches the entire line for `*/` (pattern 1's end). Not found.
- The entire line is pushed as a single `"comment"` token: `{ text: "   multi-line", type: "comment" }`.
- Return immediately with the same state: `{ rangeIdx: 1 }`.

**Line 3:** `   comment */ var y = 10;`
- State is `{ rangeIdx: 1 }`. Phase 1 runs.
- The tokenizer searches for `*/` and finds it at index 12.
- Everything up to and including `*/` is pushed as a `"comment"` token: `{ text: "   comment */", type: "comment" }`.
- `pos` is set to 14 (past the `*/`). State resets to `{ rangeIdx: null }`.
- The rest of the line (` var y = 10;`) is tokenized normally — `var` as keyword, `y` as symbol, etc.

This approach handles any multi-line construct: block comments (`/* */`), Python triple-quoted strings (`""" """`), Lua long strings (`[[ ]]`), Markdown fenced code blocks (` ``` ```), and HTML comments (`<!-- -->`). Each is specified as a range pattern with a start regex and an end regex, and the tokenizer handles them uniformly.

The state is minimal — a single integer (or null). This is what makes the incremental highlighting optimization possible. We can compare two states with a simple equality check: if the state at the end of a line has not changed, the highlighting of subsequent lines is unaffected.

The range pattern mechanism supports any construct that has a clear start delimiter and a clear end delimiter. It does not support nested ranges — you cannot nest one `/* */` comment inside another, because the tokenizer stops at the first `*/` it finds. In C, nested block comments are not legal, so this is correct. In languages that do support nested comments (like Rust's `/* /* */ */`), a more sophisticated approach would be needed — for example, using a counter in the state to track nesting depth. Our simple `rangeIdx` state does not support this, and we accept the limitation.

The range pattern also does not support escape characters within ranges. For example, in a string `"hello \" world"`, the escaped quote should not end the string. Our string patterns handle this correctly because they use single-line regex patterns with `(?:[^"\\]|\\.)*`, which explicitly handles escapes within the pattern itself. But a range pattern like `{ start: /"/, end: /"/ }` would break on escaped quotes, because the end pattern would match the first `"` it finds, escaped or not. This is why we use single-line regex patterns for strings wherever possible, and reserve range patterns for constructs like comments where the end delimiter is unambiguous.

Python's triple-quoted strings are an exception — they are range patterns because they genuinely span multiple lines and their delimiter (`"""`) is distinctive enough to be unambiguous. The rare case of `\"\"\"` inside a triple-quoted string would confuse the tokenizer, but this is exceedingly uncommon in practice.


## 7.5 — The Highlight Cache

The highlight cache, owned by the `Doc` class, stores the tokenization results for every line in the document. Without it, we would need to tokenize every visible line from scratch on every frame — which would mean re-running the tokenizer on 40 or more lines, 60 times per second. With the cache, we tokenize each line once and reuse the result until the line changes.

The cache is populated at document load time by `_highlightAll`, which tokenizes every line sequentially:

```javascript
_highlightAll() {
  this.hlCache = [];
  let state = null;
  for (let i = 0; i < this.lines.length; i++) {
    const result = this.tokenizer.tokenize(this.lines[i], state);
    this.hlCache[i] = result;
    state = result.state;
  }
}
```

The sequential order is essential because each line's tokenization depends on the state from the previous line. Line 0 starts with state `null` (not inside any range). Each subsequent line uses the state returned by the previous line's tokenization.

After an edit, `_rehighlightFrom` re-tokenizes incrementally:

```javascript
_rehighlightFrom(line) {
  let state = null;
  if (line > 0 && this.hlCache[line - 1]) {
    state = this.hlCache[line - 1].state;
  }
  for (let i = line; i < this.lines.length; i++) {
    const result = this.tokenizer.tokenize(this.lines[i], state);
    const old = this.hlCache[i];
    this.hlCache[i] = result;
    state = result.state;
    if (old && i > line
        && JSON.stringify(old.state)
           === JSON.stringify(result.state)) {
      break;
    }
  }
}
```

The key optimization is the early exit. After re-tokenizing a line, we compare the new state with the old cached state. If they match, it means the edit did not change the tokenizer's transition between this line and the next. Since the next line's text has not changed either (only the edited line changed), its tokenization will produce the same result as before, so we can stop re-tokenizing.

This optimization makes the common case — typing a character in the middle of a line — effectively O(1) for the highlight update. Only the edited line needs re-tokenizing. The state at the end of the line is the same (typing `x` in the middle of a line does not change whether the line ends inside a block comment), so the early exit triggers immediately.

The worst case is when an edit changes the state transition. For example, typing `/*` opens a block comment. The state at the end of the edited line changes from `{ rangeIdx: null }` to `{ rangeIdx: 1 }`. The next line's state also changes (it was not inside a comment before, but now it is), so we re-tokenize it. This cascade continues until we find a `*/` that closes the comment, at which point the state returns to `{ rangeIdx: null }`, matches the old cached state, and the early exit triggers.

In the absolute worst case — opening a block comment on the first line of a million-line file with no closing `*/` — the re-tokenization would process every line. But this is a pathological case. In practice, block comments are closed relatively soon, and the cascade is short. Typical editing — typing characters, deleting words, adding or removing lines — changes the state on zero or one line boundaries, causing zero or one additional lines to be re-tokenized. The incremental update is almost always O(1) in the number of lines processed.

There is another scenario worth considering: what happens when the user pastes a large block of code that contains a `/*` but no `*/`? The insertion modifies the line array and splices `null` entries into the highlight cache for the new lines. When the renderer requests tokens for the visible lines, `getTokensForLine` detects the `null` entries and calls `_rehighlightFrom`. The re-tokenization starts at the first modified line and cascades forward until the state stabilizes. If the pasted block opens an unclosed comment, the cascade continues to the end of the file — but this only happens once, and subsequent edits will be incremental from there.

*Lite* uses a more sophisticated approach with cooperative threading — a background coroutine performs incremental highlighting up to a `max_wanted_line`, yielding control between chunks to keep the UI responsive. Our approach is simpler: we re-tokenize synchronously when a line is edited, relying on the early exit to keep it fast, and we tokenize on demand when `getTokensForLine` is called for a line with no cached result. This is fast enough for files of reasonable size (tens of thousands of lines), and the code is much simpler than a threading system.


## 7.6 — Language Definitions

Each language in our editor is defined by a `SyntaxDef` with a list of patterns. Let us examine the design decisions behind several of these definitions, focusing on the tricky parts and the trade-offs.

**JavaScript** is the most complex definition, with patterns for line comments, block comments, three kinds of strings (template literals, double-quoted, single-quoted), numbers (including hex, binary, octal, and scientific notation), literals (`true`, `false`, `null`, `undefined`), keywords, built-in globals, function calls, plain identifiers, and operators. The pattern order is critical: comments before operators (so `//` is not parsed as two division operators), keywords before identifiers (so `function` is not parsed as a function call), and function-call identifiers before plain identifiers (so `foo(` gets the function color).

The string patterns use the regex `"(?:[^"\\]|\\.)*"` — a double-quote, followed by any number of non-quote/non-backslash characters or escaped characters (backslash followed by anything), followed by a closing double-quote. The `(?:[^"\\]|\\.)*` construct handles escape sequences correctly: `"hello \"world\""` is recognized as a single string, not as `"hello \"` followed by text.

Template literals (backtick strings) are handled as single-line patterns rather than range patterns. This means a template literal that spans multiple lines will only be highlighted correctly on the first line. A proper implementation would use a range pattern with `` start: /`/ `` and `` end: /`/ ``, but backtick strings can contain `${expression}` interpolations, which complicates the end-pattern matching. The single-line approach is a pragmatic simplification.

**HTML** has patterns for comments (`<!-- -->`), tag names (`<div`, `</span`), self-closing and closing brackets (`/>`, `>`), attributes (`class=`, `id=`), quoted strings, and HTML entities (`&amp;`, `&lt;`). The tag name pattern `/<\/?[a-zA-Z][a-zA-Z0-9-]*/` matches both opening and closing tags, coloring them as keywords. Attributes followed by `=` are colored as `keyword2`, making the structure of a tag — name, attributes, values — visually distinct. HTML comments use a range pattern because they can span multiple lines.

**CSS** repurposes the token types creatively. Property names (matched by the lookahead `/[a-zA-Z-]+(?=\s*:)/`) are colored as `func` (blue), selectors starting with `.` or `#` are `keyword2` (purple), at-rules like `@media` are `keyword` (yellow), and numeric values with units (`px`, `em`, `rem`, `%`) are `number` (orange). This assignment is not semantically perfect — a CSS property name is not a function — but it produces a visually informative display where each syntactic role has a distinct color. The block comment pattern `/* */` handles CSS comments, which follow the same syntax as C comments.

**Python** has triple-quoted strings (`"""` and `'''`) as range patterns, which correctly handles Python's multi-line strings. The line comment pattern (`#.*`) must be ordered carefully relative to string patterns. In our definition, triple-quoted string patterns come before the line comment pattern, so `"""` on a line is recognized as a string delimiter before `#` could match as a comment. For single-line strings, the patterns come after the comment pattern, which is technically incorrect for the rare case of a `#` inside a string on the same line — but this is a minor imprecision that does not affect readability in practice.

**JSON** has an interesting pattern: `/"(?:[^"\\]|\\.)*"\s*(?=:)/` for keys, separate from the string pattern `/"(?:[^"\\]|\\.)*"/` for values. The lookahead `(?=:)` matches only when the string is followed by a colon (with optional whitespace), which is the JSON convention for keys. This gives keys a different color from values, making JSON much more readable. JSON has no comments, no multi-line constructs, and only five token types, making it the simplest syntax definition after Plain.

**Lua** is included because *lite* is written in Lua, and it showcases Lua's unique comment and string syntax. Block comments use `--[[` to open and `]]` to close, which is a range pattern. Long strings use `[[` and `]]`. Note that the block comment pattern must come before the line comment pattern (`--.*`), because `--[[` starts with `--` and would otherwise be matched as a line comment.

**C/C++** includes preprocessor directives as a special case: `/#\s*\w+/` matches `#include`, `#define`, `#ifdef`, and similar. These are colored as `keyword2` (purple), distinct from language keywords, because they are part of the preprocessing layer rather than the language itself.

**Markdown** is interesting because its patterns are line-oriented. Headings (`/^#{1,6}\s.*/`), list items (`/^\s*[-*+]\s/`), and blockquotes (`/^>\s.*/`) are anchored to the start of the line with `^`. Fenced code blocks use a range pattern with `` /^```/ `` for both start and end — a line that consists of three backticks toggles in and out of a code block.

**Rust** includes a pattern for macro invocations: `/[a-zA-Z_][a-zA-Z0-9_]*(?=\s*[!(])/` — an identifier followed by `!` or `(`. This matches `println!`, `vec![]`, and `format!()` as function calls, giving them the function color.

**Plain** is the fallback syntax with no patterns. Every character is type `"normal"`, and the entire document renders in the default text color. This is used for files with unrecognized extensions and for the "no highlighting" state.

Each definition is a balance between correctness and simplicity. A fully correct JavaScript parser would need to distinguish between division operators and regex literals, handle JSX syntax, and understand template literal nesting. Our regex-based approach cannot handle all of these cases, but it handles the common cases correctly and produces readable highlighting for real-world code. This is the same trade-off that *lite* makes — and indeed, that most lightweight editors make. Full syntactic accuracy requires a real parser, which is the domain of language servers and heavyweight editors like VS Code.


## 7.7 — Syntax Detection and Switching

When the user opens a file, the editor needs to decide which syntax to use. This is done by matching the filename's extension against the extensions registered by each syntax definition:

```javascript
function detectSyntax(filename) {
  if (!filename) return Syntaxes[Syntaxes.length - 1];
  const lower = filename.toLowerCase();
  for (let i = 0; i < Syntaxes.length; i++) {
    for (let e = 0; e < Syntaxes[i].extensions.length; e++) {
      if (lower.endsWith(Syntaxes[i].extensions[e]))
        return Syntaxes[i];
    }
  }
  return Syntaxes[Syntaxes.length - 1];
}
```

The search is case-insensitive (via `toLowerCase()`) and uses `endsWith` rather than exact matching, so `main.test.js` matches the `.js` extension. The first matching syntax wins, which means if two syntaxes share an extension, the one that appears first in the `Syntaxes` array takes precedence. The fallback is always "Plain" — the last entry in the array.

The user can also override the detected syntax through the View menu. The menu lists all available syntaxes with a bullet indicator next to the active one. Selecting a different syntax calls `_setSyntax`:

```javascript
_setSyntax(syntaxIdx) {
  if (syntaxIdx < 0 || syntaxIdx >= Syntaxes.length) return;
  this.doc.syntax = Syntaxes[syntaxIdx];
  this.doc.tokenizer.setSyntax(this.doc.syntax);
  this.doc.hlCache = [];
  this.doc._highlightAll();
  this.needsRedraw = true;
}
```

Changing the syntax clears the entire highlight cache and re-tokenizes from scratch. This is necessary because the new syntax has entirely different patterns — every line's tokens may change. The re-tokenization is synchronous and completes before the next frame is drawn, so the user sees the new highlighting immediately.

The View menu also includes a "Syntax Highlighting" toggle that enables or disables highlighting entirely. When disabled, the rendering code uses `Theme.normal` for all tokens instead of looking up the token type's color:

```javascript
const color = this.highlightEnabled
  ? tokenTypeToColor(tokens[t].type)
  : Theme.normal;
```

The tokens are still computed — the tokenizer still runs and the cache is still maintained — but the color mapping is bypassed. This means re-enabling highlighting is instant: the cached tokens already exist, and they just need to be drawn in color instead of monochrome.

The status bar shows the active syntax name, and when highlighting is disabled, it appends "(off)":

```javascript
const syntaxLabel = this.highlightEnabled
  ? this.doc.syntax.name
  : this.doc.syntax.name + " (off)";
```

This gives the user a persistent visual indicator of the current highlighting state, which is useful when editing a file with an unusual extension that might have been auto-detected incorrectly.


## 7.8 — The Connection to Rendering

The syntax highlighting system produces tokens. The rendering system consumes them. The bridge between the two is the `getTokensForLine` method and the `tokenTypeToColor` function.

During each frame, the editor's text area drawing code iterates over the visible lines:

```javascript
for (let i = startLine; i < endLine; i++) {
  const y = this.menuBarH + (i * this.lineH - this.scrollY);
  const tokens = this.doc.getTokensForLine(i);
  const runs = [];
  for (let t = 0; t < tokens.length; t++) {
    const color = this.highlightEnabled
      ? tokenTypeToColor(tokens[t].type)
      : Theme.normal;
    runs.push({ text: tokens[t].text, color: color });
  }
  this.atlas.drawColoredText(ctx, runs, textX - this.scrollX, y);
}
```

For each visible line, we get the tokens (from the cache, or freshly tokenized if needed), convert them to `{ text, color }` runs, and pass them to the font atlas's `drawColoredText` method. This is the same rendering pipeline we built in Chapter 2 — the tokens are the data source, and the font atlas is the renderer.

The conversion from tokens to runs is simple because the data structures are aligned by design. A token has `text` and `type`. A run has `text` and `color`. The only transformation is looking up the color from the type. This alignment is not accidental — we designed the token format to match what the renderer expects, minimizing the work done per frame.

The performance of this pipeline is bounded by the number of visible lines and the number of tokens per line. A typical screen shows 40 lines, and a typical line has 5–10 tokens. That is 200–400 token-to-run conversions per frame, each involving a property lookup in the `Theme` object — negligible cost. The actual rendering is dominated by the font atlas's `drawImage` calls, which are proportional to the number of color runs (not the number of characters).

One subtle performance consideration: the normal-token coalescing in the tokenizer (where consecutive unmatched characters are merged into a single `"normal"` token) directly affects rendering performance. Without coalescing, a line of 80 unmatched characters would produce 80 single-character tokens, which would become 80 separate `_drawTintedRun` calls — 80 clear-stamp-tint-blit cycles. With coalescing, those 80 characters are a single token, drawn in a single cycle. The coalescing happens during tokenization, so the cost is amortized over the line's lifetime in the cache. This is another example of the design principle that runs through the editor: pay the cost once, at the point of change, and make the per-frame rendering as fast as possible.


## 7.9 — What We Have, and What Comes Next

We now have a complete syntax highlighting system. The `SyntaxDef` class defines languages through pattern arrays. The `Tokenizer` class converts lines of text into colored tokens using a first-match pattern scan with multi-line range state. The highlight cache on the `Doc` class stores tokenization results and updates incrementally, with an early-exit optimization that limits re-tokenization to the affected lines. Ten languages are defined: JavaScript, HTML, CSS, Python, JSON, Lua, C/C++, Markdown, Rust, and Plain. Syntax is detected automatically from file extensions and can be switched manually through the View menu. Highlighting can be toggled on and off.

The system follows the same philosophy as the rest of the editor: simple data structures, straightforward algorithms, and separation of concerns. The syntax definitions are just data. The tokenizer is a stateless function. The cache is a flat array. There are no abstract syntax trees, no parser generators, no grammar specifications. The regex-based approach has limitations — it cannot handle context-sensitive constructs like template literal nesting or regex-vs-division ambiguity — but it produces correct and readable highlighting for the vast majority of real-world code, and it does so in a hundred lines of code.

In Chapter 8, we will build the visual layout of the editor: the gutter with line numbers, the status bar, the active-line highlight, the selection rectangles, and the scrollbar. These are the visual elements that surround the text and give the editor its structure and identity — the chrome that makes our bare canvas feel like a real application.
