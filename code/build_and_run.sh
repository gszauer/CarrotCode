rm ./carrotcode 

g++ -o carrotcode linux.cpp document.cpp syntax.cpp strings.cpp debug_renderer.cpp imgui.cpp view.cpp application.cpp -lX11

./carrotcode