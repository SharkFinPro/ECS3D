# ECS3D

This project focuses on developing a robust 3D Entity Component System, which is seamlessly integrated with a high-performance 3D rendering engine.

## Build Instructions

### Prerequisites

Before building, ensure you have the following dependencies installed:

1. **CMake** (version 3.29 or higher)
2. **Vulkan SDK** (latest version recommended)
3. **Git** (for cloning the repository)

### Cloning the Repository and Building the Project

1. Clone the Repository

First, clone the repository to your local machine:

```bash
git clone https://github.com/SharkFinPro/ECS3D.git
cd ECS3D
```

2. Create a Build Directory

Create a separate directory for the build process:

```bash
mkdir build
cd build
```

3. Generate Build Files with CMake

Configure the CMake project and generate the necessary build files:

```bash
cmake ..
```

4. Build the Project

Compile the project using your preferred build system:

```bash
cmake --build .
```

5. Run the Executable

After building, all files will have been written to the `bin` directory. You can run the cube example with:

```bash
cd bin
./setupTest
```
