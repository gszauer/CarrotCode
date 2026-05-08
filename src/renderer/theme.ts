import type { Color } from "../shared/types";

export type ThemeName = "dark" | "light";
export type ThemePalette = Record<keyof typeof darkTheme, Color>;

const darkTheme = {
  background: [0.12, 0.13, 0.15, 1] as Color,
  panel: [0.15, 0.16, 0.18, 1] as Color,
  panel2: [0.18, 0.19, 0.22, 1] as Color,
  activity: [0.10, 0.11, 0.13, 1] as Color,
  activityActive: [0.22, 0.26, 0.31, 1] as Color,
  divider: [0.24, 0.25, 0.28, 1] as Color,
  text: [0.84, 0.86, 0.90, 1] as Color,
  textDim: [0.54, 0.58, 0.64, 1] as Color,
  accent: [0.31, 0.57, 0.91, 1] as Color,
  accent2: [0.46, 0.76, 0.47, 1] as Color,
  warning: [0.95, 0.68, 0.28, 1] as Color,
  error: [0.93, 0.35, 0.38, 1] as Color,
  selection: [0.22, 0.39, 0.65, 0.78] as Color,
  caret: [0.95, 0.95, 0.95, 1] as Color,
  lineHighlight: [0.18, 0.20, 0.23, 1] as Color,
  keyword: [0.76, 0.52, 0.95, 1] as Color,
  string: [0.67, 0.82, 0.54, 1] as Color,
  number: [0.93, 0.70, 0.47, 1] as Color,
  comment: [0.45, 0.50, 0.56, 1] as Color,
  operator: [0.74, 0.78, 0.85, 1] as Color,
  function: [0.53, 0.72, 0.95, 1] as Color,
  type: [0.48, 0.83, 0.75, 1] as Color
};

const lightTheme: ThemePalette = {
  background: [0.82, 0.84, 0.87, 1],
  panel: [0.73, 0.76, 0.80, 1],
  panel2: [0.86, 0.88, 0.91, 1],
  activity: [0.66, 0.70, 0.75, 1],
  activityActive: [0.55, 0.64, 0.74, 1],
  divider: [0.48, 0.53, 0.60, 1],
  text: [0.08, 0.10, 0.13, 1],
  textDim: [0.28, 0.32, 0.38, 1],
  accent: [0.08, 0.34, 0.68, 1],
  accent2: [0.13, 0.43, 0.19, 1],
  warning: [0.58, 0.32, 0.04, 1],
  error: [0.64, 0.10, 0.13, 1],
  selection: [0.38, 0.58, 0.82, 0.66],
  caret: [0.08, 0.10, 0.14, 1],
  lineHighlight: [0.75, 0.79, 0.84, 1],
  keyword: [0.35, 0.12, 0.62, 1],
  string: [0.18, 0.36, 0.06, 1],
  number: [0.52, 0.25, 0.04, 1],
  comment: [0.34, 0.38, 0.44, 1],
  operator: [0.18, 0.21, 0.27, 1],
  function: [0.03, 0.28, 0.58, 1],
  type: [0.02, 0.38, 0.35, 1]
};

export const themes: Record<ThemeName, ThemePalette> = {
  dark: darkTheme,
  light: lightTheme
};

export const theme: ThemePalette = { ...darkTheme };

export function applyTheme(name: ThemeName): void {
  Object.assign(theme, themes[name]);
}
