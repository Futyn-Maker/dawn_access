#include "features/Shapes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace da::shapes
{
    namespace
    {
        constexpr double kFar = std::numeric_limits<double>::infinity();

        Vec Add(const Vec& a, const Vec& b)
        {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }
        Vec Sub(const Vec& a, const Vec& b)
        {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }
        Vec Mul(const Vec& a, double k)
        {
            return {a.x * k, a.y * k, a.z * k};
        }
        double Dot(const Vec& a, const Vec& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
        Vec Cross(const Vec& a, const Vec& b)
        {
            return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        }

        // The point of the segment ab nearest p.
        Vec NearestOnSegment(const Vec& p, const Vec& a, const Vec& b)
        {
            const Vec ab = Sub(b, a);
            const double length = Dot(ab, ab);
            if (length <= 0.0) return a;
            return Add(a, Mul(ab, std::clamp(Dot(Sub(p, a), ab) / length, 0.0, 1.0)));
        }

        // The gap between the segments p1q1 and p2q2, from the nearest point of the second to the
        // nearest point of the first (Ericson, Real-Time Collision Detection, 5.1.9).
        Vec SegmentsGap(const Vec& p1, const Vec& q1, const Vec& p2, const Vec& q2)
        {
            constexpr double eps = 1e-9;
            const Vec d1 = Sub(q1, p1);
            const Vec d2 = Sub(q2, p2);
            const Vec r = Sub(p1, p2);
            const double a = Dot(d1, d1);
            const double e = Dot(d2, d2);
            const double f = Dot(d2, r);
            double s = 0.0;
            double t = 0.0;
            if (a <= eps && e <= eps)
            {
            }
            else if (a <= eps)
            {
                t = std::clamp(f / e, 0.0, 1.0);
            }
            else
            {
                const double c = Dot(d1, r);
                if (e <= eps)
                {
                    s = std::clamp(-c / a, 0.0, 1.0);
                }
                else
                {
                    const double b = Dot(d1, d2);
                    const double denom = a * e - b * b;
                    s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
                    t = (b * s + f) / e;
                    if (t < 0.0)
                    {
                        t = 0.0;
                        s = std::clamp(-c / a, 0.0, 1.0);
                    }
                    else if (t > 1.0)
                    {
                        t = 1.0;
                        s = std::clamp((b - c) / a, 0.0, 1.0);
                    }
                }
            }
            return Sub(Add(p1, Mul(d1, s)), Add(p2, Mul(d2, t)));
        }

        double SegmentsApart(const Vec& p1, const Vec& q1, const Vec& p2, const Vec& q2)
        {
            const Vec gap = SegmentsGap(p1, q1, p2, q2);
            return std::sqrt(Dot(gap, gap));
        }

        // The corners of a simplex of the distance search.
        struct Simplex
        {
            Vec p[4];
            int n = 0;
        };

        Simplex Of(std::initializer_list<Vec> corners)
        {
            Simplex s;
            for (const Vec& c : corners)
                s.p[s.n++] = c;
            return s;
        }

        // The point of segment ab nearest the origin; `keep` gets the corners it lies between.
        Vec NearestOnEdge(const Vec& a, const Vec& b, Simplex& keep)
        {
            const Vec ab = Sub(b, a);
            const double length = Dot(ab, ab);
            const double t = length > 0.0 ? -Dot(a, ab) / length : 0.0;
            if (t <= 0.0)
            {
                keep = Of({a});
                return a;
            }
            if (t >= 1.0)
            {
                keep = Of({b});
                return b;
            }
            keep = Of({a, b});
            return Add(a, Mul(ab, t));
        }

        // The point of triangle abc nearest the origin, by the region of the triangle's plane it
        // falls in (Ericson 5.1.5); `keep` gets the corners it lies among.
        Vec NearestOnTriangle(const Vec& a, const Vec& b, const Vec& c, Simplex& keep)
        {
            const Vec ab = Sub(b, a);
            const Vec ac = Sub(c, a);
            const Vec ap = Mul(a, -1.0);
            const double d1 = Dot(ab, ap);
            const double d2 = Dot(ac, ap);
            if (d1 <= 0.0 && d2 <= 0.0)
            {
                keep = Of({a});
                return a;
            }
            const Vec bp = Mul(b, -1.0);
            const double d3 = Dot(ab, bp);
            const double d4 = Dot(ac, bp);
            if (d3 >= 0.0 && d4 <= d3)
            {
                keep = Of({b});
                return b;
            }
            const double vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 && d1 - d3 > 0.0)
            {
                keep = Of({a, b});
                return Add(a, Mul(ab, d1 / (d1 - d3)));
            }
            const Vec cp = Mul(c, -1.0);
            const double d5 = Dot(ab, cp);
            const double d6 = Dot(ac, cp);
            if (d6 >= 0.0 && d5 <= d6)
            {
                keep = Of({c});
                return c;
            }
            const double vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 && d2 - d6 > 0.0)
            {
                keep = Of({a, c});
                return Add(a, Mul(ac, d2 / (d2 - d6)));
            }
            const double va = d3 * d6 - d5 * d4;
            if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0 && (d4 - d3) + (d5 - d6) > 0.0)
            {
                keep = Of({b, c});
                return Add(b, Mul(Sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
            }
            const double sum = va + vb + vc;
            if (!(sum > 1e-12 * (Dot(ab, ab) * Dot(ac, ac) + 1.0)))
            {
                // A triangle with no area: the nearest of its edges.
                Simplex k1, k2, k3;
                const Vec e1 = NearestOnEdge(a, b, k1);
                const Vec e2 = NearestOnEdge(a, c, k2);
                const Vec e3 = NearestOnEdge(b, c, k3);
                const double n1 = Dot(e1, e1), n2 = Dot(e2, e2), n3 = Dot(e3, e3);
                if (n1 <= n2 && n1 <= n3)
                {
                    keep = k1;
                    return e1;
                }
                if (n2 <= n3)
                {
                    keep = k2;
                    return e2;
                }
                keep = k3;
                return e3;
            }
            keep = Of({a, b, c});
            return Add(a, Add(Mul(ab, vb / sum), Mul(ac, vc / sum)));
        }

        // Whether the origin lies on the far side of face abc from d, or on the face itself; a
        // flat tetrahedron has every face so.
        bool Beyond(const Vec& a, const Vec& b, const Vec& c, const Vec& d)
        {
            const Vec n = Cross(Sub(b, a), Sub(c, a));
            return Dot(Mul(a, -1.0), n) * Dot(Sub(d, a), n) <= 0.0;
        }

        // The point of tetrahedron abcd nearest the origin (Ericson 5.1.6): the origin itself when
        // inside, else the nearest point of the faces it lies beyond.
        Vec NearestOnTetrahedron(const Vec& a, const Vec& b, const Vec& c, const Vec& d, Simplex& keep, bool& inside)
        {
            inside = true;
            double best = kFar;
            Vec nearest;
            const Vec faces[4][4] = {{a, b, c, d}, {a, c, d, b}, {a, d, b, c}, {b, d, c, a}};
            for (const auto& f : faces)
            {
                if (!Beyond(f[0], f[1], f[2], f[3])) continue;
                inside = false;
                Simplex k;
                const Vec q = NearestOnTriangle(f[0], f[1], f[2], k);
                const double dq = Dot(q, q);
                if (dq < best)
                {
                    best = dq;
                    nearest = q;
                    keep = k;
                }
            }
            if (inside) keep = Of({a, b, c, d});
            return inside ? Vec{} : nearest;
        }
    }

    Quat FromRotator(double pitch, double yaw, double roll)
    {
        const double half = std::numbers::pi / 360.0;
        const double sp = std::sin(pitch * half), cp = std::cos(pitch * half);
        const double sy = std::sin(yaw * half), cy = std::cos(yaw * half);
        const double sr = std::sin(roll * half), cr = std::cos(roll * half);
        // FRotator::Quaternion.
        return {cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy};
    }

    Vec Turn(const Quat& q, const Vec& v)
    {
        // FQuat::RotateVector: v + 2w(q x v) + 2q x (q x v).
        const Vec axis{q.x, q.y, q.z};
        const Vec t = Mul(Cross(axis, v), 2.0);
        return Add(Add(v, Mul(t, q.w)), Cross(axis, t));
    }

    Vec Place(const Pose& pose, const Vec& local)
    {
        const Vec scaled{local.x * pose.scale.x, local.y * pose.scale.y, local.z * pose.scale.z};
        return Add(Turn(pose.rotation, scaled), pose.translation);
    }

    bool Shape::Empty() const
    {
        return hulls.empty() && balls.empty() && pills.empty();
    }

    void Shape::Bounds(Vec& low, Vec& high) const
    {
        low = Vec{kFar, kFar, kFar};
        high = Vec{-kFar, -kFar, -kFar};
        const auto take = [&](const Vec& p, double r)
        {
            low = Vec{std::min(low.x, p.x - r), std::min(low.y, p.y - r), std::min(low.z, p.z - r)};
            high = Vec{std::max(high.x, p.x + r), std::max(high.y, p.y + r), std::max(high.z, p.z + r)};
        };
        for (const auto& hull : hulls)
        {
            for (const auto& p : hull.points)
                take(p, 0.0);
        }
        for (const auto& ball : balls)
            take(ball.middle, ball.radius);
        for (const auto& pill : pills)
        {
            take(pill.a, pill.radius);
            take(pill.b, pill.radius);
        }
    }

    Hull BoxHull(const Vec& middle, const Vec& half, const Quat& turn)
    {
        Hull hull;
        for (const double sx : {-1.0, 1.0})
        {
            for (const double sy : {-1.0, 1.0})
            {
                for (const double sz : {-1.0, 1.0})
                    hull.points.push_back(Add(middle, Turn(turn, Vec{sx * half.x, sy * half.y, sz * half.z})));
            }
        }
        return hull;
    }

    double Distance(const Hull& hull, const Vec& a0, const Vec& b0)
    {
        Vec gap;
        return HullGap(hull, a0, b0, gap);
    }

    double HullGap(const Hull& hull, const Vec& a0, const Vec& b0, Vec& gap)
    {
        gap = Vec{};
        if (hull.points.empty()) return kFar;
        // Measured about a corner of the hull, for precision far from the world's origin.
        const Vec o = hull.points.front();
        const Vec a = Sub(a0, o);
        const Vec b = Sub(b0, o);
        // The point of the difference (the segment less the hull) furthest along d.
        const auto support = [&](const Vec& d)
        {
            const Vec& onSegment = Dot(a, d) >= Dot(b, d) ? a : b;
            const Vec* lowest = &hull.points.front();
            double low = kFar;
            for (const Vec& p : hull.points)
            {
                const double k = Dot(Sub(p, o), d);
                if (k < low)
                {
                    low = k;
                    lowest = &p;
                }
            }
            return Sub(onSegment, Sub(*lowest, o));
        };
        // The search begins at a point of the difference, the simplex holding it.
        Vec v = Sub(a, Sub(hull.points.front(), o));
        Simplex s = Of({v});
        for (int i = 0; i < 64; ++i)
        {
            const double vv = Dot(v, v);
            if (vv <= 1e-12)
            {
                gap = Vec{};
                return 0.0;
            }
            const Vec w = support(Mul(v, -1.0));
            // No point of the difference lies nearer the origin than v by more than the tolerance.
            if (vv - Dot(v, w) <= 1e-9 * vv + 1e-9) break;
            bool known = false;
            for (int k = 0; k < s.n; ++k)
            {
                const Vec gap = Sub(w, s.p[k]);
                if (Dot(gap, gap) <= 1e-12) known = true;
            }
            if (known) break;
            s.p[s.n++] = w;
            Simplex keep;
            bool inside = false;
            Vec next;
            switch (s.n)
            {
            case 1:
                next = w;
                keep = s;
                break;
            case 2: next = NearestOnEdge(s.p[0], s.p[1], keep); break;
            case 3: next = NearestOnTriangle(s.p[0], s.p[1], s.p[2], keep); break;
            default: next = NearestOnTetrahedron(s.p[0], s.p[1], s.p[2], s.p[3], keep, inside); break;
            }
            if (inside)
            {
                gap = Vec{};
                return 0.0;
            }
            s = keep;
            if (Dot(next, next) >= vv) break;
            v = next;
        }
        // v is a point of the segment less a point of the hull: the hull lies the other way.
        gap = Mul(v, -1.0);
        return std::sqrt(Dot(v, v));
    }

    Gap Nearest(const Shape& shape, const Vec& a, const Vec& b)
    {
        Gap best;
        best.distance = kFar;
        for (const auto& hull : shape.hulls)
        {
            Vec toward;
            const double d = HullGap(hull, a, b, toward);
            if (d < best.distance)
            {
                best.distance = d;
                best.toward = toward;
            }
        }
        for (const auto& ball : shape.balls)
        {
            const Vec onSegment = NearestOnSegment(ball.middle, a, b);
            const Vec gap = Sub(ball.middle, onSegment);
            const double length = std::sqrt(Dot(gap, gap));
            const double d = std::max(0.0, length - ball.radius);
            if (d < best.distance)
            {
                best.distance = d;
                best.toward = length > 0.0 ? Mul(gap, d / length) : Vec{};
            }
        }
        for (const auto& pill : shape.pills)
        {
            const Vec gap = Mul(SegmentsGap(a, b, pill.a, pill.b), -1.0);
            const double length = std::sqrt(Dot(gap, gap));
            const double d = std::max(0.0, length - pill.radius);
            if (d < best.distance)
            {
                best.distance = d;
                best.toward = length > 0.0 ? Mul(gap, d / length) : Vec{};
            }
        }
        return best;
    }

    double Distance(const Shape& shape, const Vec& a, const Vec& b)
    {
        double best = kFar;
        for (const auto& hull : shape.hulls)
            best = std::min(best, Distance(hull, a, b));
        for (const auto& ball : shape.balls)
        {
            const Vec gap = Sub(NearestOnSegment(ball.middle, a, b), ball.middle);
            best = std::min(best, std::max(0.0, std::sqrt(Dot(gap, gap)) - ball.radius));
        }
        for (const auto& pill : shape.pills)
            best = std::min(best, std::max(0.0, SegmentsApart(a, b, pill.a, pill.b) - pill.radius));
        return best;
    }

    bool CapsuleTouches(const Shape& shape, const Vec& middle, double radius, double halfHeight)
    {
        const double half = std::max(0.0, halfHeight - radius);
        return Distance(shape, Vec{middle.x, middle.y, middle.z - half}, Vec{middle.x, middle.y, middle.z + half}) <= radius;
    }

    bool SweepTouches(const Shape& shape, const Vec& from, const Vec& to, double radius, double halfHeight)
    {
        const double rise = std::max(0.0, halfHeight - radius);
        const auto apart = [&](const Vec& m) { return Distance(shape, Vec{m.x, m.y, m.z - rise}, Vec{m.x, m.y, m.z + rise}); };
        const double start = apart(from);
        const Vec half{(from.x + to.x) / 2.0, (from.y + to.y) / 2.0, (from.z + to.z) / 2.0};
        const double mid = apart(half);
        const double end = apart(to);
        // A capsule already against the shape may move away from it, as the engine's sweep lets a
        // capsule that starts in penetration move out.
        return (mid <= radius && mid < start) || (end <= radius && end <= start);
    }
}
