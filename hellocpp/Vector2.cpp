#include <emscripten/bind.h>

using namespace emscripten;


class Vector2
{
private:
    double x;
    double y;
    /* data */
public:
    Vector2(double, double);
    ~Vector2();
    Vector2 add(const Vector2&);
    double dot(const Vector2&);
    double getx();
    double gety();
};

Vector2::Vector2(double _x, double _y)
{
    this->x = _x;
    this->y = _y;
}

Vector2::~Vector2()
{
}

Vector2 Vector2::add(const Vector2& v)
{
    this-> x = this->x + v.x;
    this-> y = this->y + v.y;
    return *this;
}

double Vector2::getx()
{
    return this->x;
}

double Vector2::gety()
{
    return this->y;
}

double Vector2::dot(const Vector2& v)
{
    return this->x*v.x + this->y*v.y;
}

// Binding code
EMSCRIPTEN_BINDINGS(Vector2_example) {
  class_<Vector2>("Vector2")
    .constructor<double, double>()
    .function("add", &Vector2::add)
    .function("dot", &Vector2::dot)
    .function("getx", &Vector2::getx)
    .function("gety", &Vector2::gety)
    ;
}