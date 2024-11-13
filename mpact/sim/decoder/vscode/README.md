# MPACT-Sim Syntax Highlighting VS Code Extensions

This directory contains two directories that provide the source code for the VS
Code extensions that enable syntax highlighting for .isa and .bin_fmt files in
VS Code.

For the time being, they are not published in the VS Code marketplace, but have
to be built and then manually installed in VS Code as detailed below.

## Building and installing

### Prerequisites

Before you can build an extension, make sure you have `node/npm` and `vsce`
installed. You should be able to install node using most package managers. Once
node is installed you use `nvm` to install `vsce`:

```
nvm install -g @vscode/vsce
```

### Building the extension

Each syntax highlighting extension is built by executing the following command
from within the syntax highlighting extension source directory (isa/binfmt):

```
vsce package --allow-missing-repository
```

This will produce a file `vscode-mpact-sim-<name>-N.N.N.vsix` that can be
installed in VS Code.

### Installing the extension

Repeat the following for each (bin_fmt and isa) extension:

1.  Open VS Code
1.  Select the *Extensions* view on the left hand side of the window.
1.  Click on the three dots (...) in the top right corner of the Extensions
    panel.
1.  Select `Install from VSIX...`
1.  Navigate to and select the `.vsix` file generated previously and click
    `Open`.

The extensions should now be installed and available.
