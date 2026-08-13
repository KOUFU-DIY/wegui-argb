import type { CSSProperties, ReactNode } from "react";

interface IconProps {
  size?: number;
  className?: string;
  style?: CSSProperties;
}

function svg(path: ReactNode, viewBox = "0 0 24 24") {
  return function Icon({ size = 18, className, style }: IconProps) {
    return (
      <svg
        className={className}
        style={style}
        width={size}
        height={size}
        viewBox={viewBox}
        fill="none"
        stroke="currentColor"
        strokeWidth="1.8"
        strokeLinecap="round"
        strokeLinejoin="round"
        aria-hidden
      >
        {path}
      </svg>
    );
  };
}

export const IconGrid = svg(
  <>
    <rect x="3.5" y="3.5" width="7" height="7" rx="1.4" />
    <rect x="13.5" y="3.5" width="7" height="7" rx="1.4" />
    <rect x="3.5" y="13.5" width="7" height="7" rx="1.4" />
    <rect x="13.5" y="13.5" width="7" height="7" rx="1.4" />
  </>,
);

export const IconFont = svg(
  <>
    <path d="M5 19 11 5h2l6 14" />
    <path d="M7.5 14.5h9" />
  </>,
);

export const IconImage = svg(
  <>
    <rect x="3.5" y="4.5" width="17" height="15" rx="2" />
    <circle cx="9" cy="10" r="1.7" />
    <path d="m3.5 16.5 4.8-4.3 4.2 4 3-2.6 5 4.4" />
  </>,
);

export const IconPackage = svg(
  <>
    <path d="M12 3 20 7v10l-8 4-8-4V7z" />
    <path d="m4 7 8 4 8-4" />
    <path d="M12 11v9" />
  </>,
);

export const IconChip = svg(
  <>
    <rect x="6.5" y="6.5" width="11" height="11" rx="1.6" />
    <path d="M9.5 3.5v3M14.5 3.5v3M9.5 17.5v3M14.5 17.5v3M3.5 9.5h3M3.5 14.5h3M17.5 9.5h3M17.5 14.5h3" />
  </>,
);

export const IconInfo = svg(
  <>
    <circle cx="12" cy="12" r="8.5" />
    <path d="M12 11v5" />
    <path d="M12 8h.01" />
  </>,
);

export const IconPlay = svg(<path d="M8 5.5v13l11-6.5z" fill="currentColor" stroke="none" />);

export const IconRefresh = svg(
  <>
    <path d="M20 12a8 8 0 1 1-2.3-5.6" />
    <path d="M20 3.5V7h-3.5" />
  </>,
);

export const IconFolderOpen = svg(
  <>
    <path d="M3.5 6.5a1.5 1.5 0 0 1 1.5-1.5h4l2 2h7a1.5 1.5 0 0 1 1.5 1.5v1" />
    <path d="M3.5 6.5V18a1 1 0 0 0 1 1h13.2a1.5 1.5 0 0 0 1.45-1.1l1.7-6a1.2 1.2 0 0 0-1.16-1.5H7.2a1.5 1.5 0 0 0-1.44 1.08L3.5 18" />
  </>,
);

export const IconChevron = svg(<path d="m9 6 6 6-6 6" />);

export const IconTerminal = svg(
  <>
    <rect x="3" y="4.5" width="18" height="15" rx="2" />
    <path d="m7 9.5 3 3-3 3" />
    <path d="M12.5 15.5H17" />
  </>,
);

export const IconTrash = svg(
  <>
    <path d="M4.5 6.5h15" />
    <path d="M9 6.5V5a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v1.5" />
    <path d="M6.5 6.5 7.4 19a1.5 1.5 0 0 0 1.5 1.4h6.2a1.5 1.5 0 0 0 1.5-1.4l.9-12.5" />
  </>,
);

export const IconArrow = svg(
  <>
    <path d="M4 12h15" />
    <path d="m13 6 6 6-6 6" />
  </>,
);

export const IconPlus = svg(
  <>
    <path d="M12 5v14M5 12h14" />
  </>,
);

export const IconClose = svg(
  <>
    <path d="m6 6 12 12M18 6 6 18" />
  </>,
);

export const IconGear = svg(
  <>
    <circle cx="12" cy="12" r="3.2" />
    <path d="M12 2.8v3M12 18.2v3M2.8 12h3M18.2 12h3M5.5 5.5l2.1 2.1M16.4 16.4l2.1 2.1M18.5 5.5l-2.1 2.1M7.6 16.4l-2.1 2.1" />
  </>,
);

export const IconLink = svg(
  <>
    <path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 1 0-5.7-5.6l-1.2 1.2" />
    <path d="M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 1 0 5.7 5.6l1.2-1.2" />
  </>,
);

export const IconFile = svg(
  <>
    <path d="M6 3.5h8l4 4V20a.9.9 0 0 1-.9.9H6.9A.9.9 0 0 1 6 20V4.4a.9.9 0 0 1 .9-.9z" />
    <path d="M14 3.5V8h4.5" />
  </>,
);

export function Spinner({ size = 14 }: { size?: number }) {
  return <span className="spinner" style={{ width: size, height: size }} />;
}

export function Logo({ size = 24 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden>
      <rect x="1" y="1" width="30" height="30" rx="7" fill="#16202c" stroke="#2c3a4c" />
      <rect x="6" y="6" width="8" height="8" rx="2" fill="#38bdf8" />
      <rect x="18" y="6" width="8" height="8" rx="2" fill="#233140" />
      <rect x="6" y="18" width="8" height="8" rx="2" fill="#233140" />
      <rect x="18" y="18" width="8" height="8" rx="2" fill="#34d399" />
    </svg>
  );
}
