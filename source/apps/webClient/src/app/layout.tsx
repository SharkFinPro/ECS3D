import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "ECS3D Client",
  description: "Browser client for an authoritative ECS3D server",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
