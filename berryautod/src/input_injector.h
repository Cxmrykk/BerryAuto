#pragma once
class InputInjector {
public:
    bool init(int width, int height);
    void inject_touch(int x, int y, bool is_down);
    ~InputInjector();
private:
    int uinput_fd = -1;
};
