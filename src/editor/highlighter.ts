export type TokenType = "normal" | "keyword" | "string" | "number" | "comment" | "operator" | "function" | "type";

export type Token = {
  type: TokenType;
  text: string;
};

type Rule = {
  type: TokenType;
  pattern: RegExp;
};

const commonRules: Rule[] = [
  { type: "comment", pattern: /^\/\/.*/ },
  { type: "comment", pattern: /^#.*/ },
  { type: "string", pattern: /^"([^"\\]|\\.)*"/ },
  { type: "string", pattern: /^'([^'\\]|\\.)*'/ },
  { type: "string", pattern: /^`([^`\\]|\\.)*`/ },
  { type: "number", pattern: /^\b\d+(?:\.\d+)?\b/ },
  { type: "operator", pattern: /^[+\-*/%=!<>:&|^~.,;()[\]{}]+/ }
];

const keywords = new Set([
  "as", "async", "await", "break", "case", "catch", "class", "const", "continue", "def",
  "default", "do", "else", "enum", "export", "extends", "false", "for", "from", "function",
  "if", "import", "in", "interface", "let", "local", "nil", "null", "public", "private",
  "return", "static", "struct", "switch", "then", "true", "try", "type", "var", "void",
  "while", "yield"
]);

const typeWords = new Set(["string", "number", "boolean", "object", "Promise", "Array", "Record", "void"]);

export class Highlighter {
  tokenizeLine(text: string, syntaxId: string): Token[] {
    if (syntaxId === "markdown") return tokenizeMarkdown(text);
    const tokens: Token[] = [];
    let i = 0;
    while (i < text.length) {
      const rest = text.slice(i);
      if (/^\s+/.test(rest)) {
        const match = rest.match(/^\s+/)![0];
        tokens.push({ type: "normal", text: match });
        i += match.length;
        continue;
      }
      const rule = commonRules.find((candidate) => candidate.pattern.test(rest));
      if (rule) {
        const match = rest.match(rule.pattern)![0];
        tokens.push({ type: rule.type, text: match });
        i += match.length;
        continue;
      }
      const word = rest.match(/^[A-Za-z_$][A-Za-z0-9_$]*/)?.[0];
      if (word) {
        const nextChar = text.charAt(i + word.length);
        const type: TokenType = keywords.has(word)
          ? "keyword"
          : typeWords.has(word)
            ? "type"
            : nextChar === "("
              ? "function"
              : "normal";
        tokens.push({ type, text: word });
        i += word.length;
        continue;
      }
      tokens.push({ type: "normal", text: rest.charAt(0) });
      i++;
    }
    return mergeTokens(tokens);
  }
}

function tokenizeMarkdown(text: string): Token[] {
  if (/^\s*#/.test(text)) return [{ type: "keyword", text }];
  if (/^\s*[-*]\s/.test(text)) return [{ type: "operator", text: text.match(/^\s*[-*]\s/)![0] }, { type: "normal", text: text.replace(/^\s*[-*]\s/, "") }];
  return mergeTokens([{ type: "normal", text }]);
}

function mergeTokens(tokens: Token[]): Token[] {
  const result: Token[] = [];
  for (const token of tokens) {
    const last = result[result.length - 1];
    if (last && last.type === token.type) last.text += token.text;
    else result.push({ ...token });
  }
  return result;
}
