# NP16 — 16-bit OS/2 VIO Text Editor

NP16 is a lightweight, keyboard-oriented text editor for **16-bit IBM OS/2**, targeting **OS/2 1.3** and its text-mode Video I/O (VIO) environment.

Written in C and compiled using **Microsoft C 6.00**, NP16 aims to provide a practical, responsive editing environment while respecting the constraints and capabilities of 16-bit OS/2.

The editor follows familiar **IBM Common User Access (CUA)** conventions, emphasizing predictable keyboard controls, straightforward navigation, and a traditional text-mode interface.

## Design Goals

- Native 16-bit OS/2 application.
- Text-mode VIO interface without Presentation Manager dependencies.
- Familiar CUA keyboard conventions.
- Efficient text handling within segmented-memory constraints.
- Responsive editing with minimal screen redraws.
- Modular design for continued development.
- Native integration with OS/2 facilities, including Extended Attributes.

## Current Development

NP16 is under active development.

The editor is being organized into separate components:

- **EDITOR.C** — User interface, keyboard handling, and editor interaction.
- **TEXT.C / TEXT.H** — Document storage and text manipulation.
- **HISTORY.C / HISTORY.H** — Editing history and Undo/Redo management.

Current development focuses on stabilizing the document engine, cursor handling, viewport management, and multi-level Undo/Redo.

## Planned Improvements

The following features are planned for future releases. They are not necessarily implemented yet.

### Editing

- Syntax highlighting for programming languages and structured text.
- Automatic indentation and configurable tab handling.
- Search and replace.
- Improved text selection and clipboard operations.
- Additional editing shortcuts.

### Files and Workspace

- Multiple open files within a single editor session.
- Split-screen editing with multiple panels.
- Switching between open documents using keyboard shortcuts.
- Detection of files modified externally.
- Improved file loading and saving.

### Reliability

- Temporary autosave to help recover unsaved work after an interruption.
- Recovery of temporary editing sessions.
- Safe file-saving procedures to reduce the risk of data loss.
- Continued improvements to Undo/Redo and memory management.

### OS/2 INI Integration

Planned support for native OS/2 profile files to preserve editor preferences and session settings, subject to compatibility with the VIO-only environment.

Potential settings include:

- Editor colours and display preferences.
- Tab width and indentation settings.
- Autosave interval.
- Recently opened files.
- Panel arrangements and workspace preferences.

### OS/2 Extended Attributes

Planned use of Extended Attributes (EAs) to associate editing metadata directly with individual files.

Potential applications include:

- Restoring cursor and viewport positions.
- Remembering the preferred syntax-highlighting language.
- Preserving file-specific tab and indentation preferences.
- Storing bookmarks and other editing metadata.
- Recording code-page information.

EA metadata should remain optional, allowing NP16 to edit ordinary files without requiring Extended Attribute support.

### Interface

- Configurable foreground and background colours using the standard 16-colour VIO palette.
- Status information for cursor position, file state, and editing mode.
- Improved support for different VIO screen dimensions.
- Context-sensitive help and keyboard shortcut reference.

## Development Environment

| Component | Target |
|---|---|
| Operating system | IBM OS/2 1.3 |
| Architecture | 16-bit x86 |
| Interface | OS/2 VIO |
| Programming language | C |
| Compiler | Microsoft C 6.00 |
| User interface | IBM CUA conventions |
| License | MIT |

## Project Philosophy

NP16 is intended to remain a native 16-bit text-mode application rather than becoming a Presentation Manager editor.

The objective is not to reproduce a modern graphical IDE, but to bring useful editing capabilities to the classic OS/2 VIO environment while retaining its simplicity, responsiveness, and keyboard-oriented operation.

## License

NP16 is distributed under the MIT License. See [LICENSE](LICENSE) for details.
