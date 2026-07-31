/********************************************************************
	created:	Thursday 2026/07/31 at 10:00
	filename: 	VectorAnimCookGeometry.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

//! @file VectorAnimCookGeometry.cpp
//! @brief the vector-animation cook's source-property sampling and path
//! geometry (@see VectorAnimCookDetail.h)

#include "core_util/VectorAnimCookDetail.h"

#include <algorithm>
#include <cstdarg>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Orkige
{
	namespace VectorAnimCookDetail
	{
		//=========================================================
		// text formatting
		//=========================================================
		//---------------------------------------------------------
		String formatText(char const * format, ...)
		{
			char buffer[512];
			va_list args;
			va_start(args, format);
			int written = vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			if (written < 0)
			{
				return String();
			}
			if (static_cast<size_t>(written) < sizeof(buffer))
			{
				return String(buffer, static_cast<size_t>(written));
			}
			std::vector<char> large(static_cast<size_t>(written) + 1);
			va_start(args, format);
			vsnprintf(large.data(), large.size(), format, args);
			va_end(args);
			return String(large.data(), static_cast<size_t>(written));
		}
		//---------------------------------------------------------
		double pyRound4(double value)
		{
			// python's round(x, 4): format to four decimals with correct
			// rounding, then read the result back as the nearest double
			if (!std::isfinite(value))
			{
				return value;
			}
			char buffer[64];
			snprintf(buffer, sizeof(buffer), "%.4f", value);
			return strtod(buffer, nullptr);
		}
		//---------------------------------------------------------
		String fmtFrame(double value)
		{
			double rounded = pyRound4(value);
			if (rounded == 0.0)
			{
				rounded = std::fabs(rounded);	// canonical zero, never "-0"
			}
			return formatText("%g", rounded);
		}
		//---------------------------------------------------------
		String fmtVal(double value)
		{
			if (std::fabs(value) < 5e-6)
			{
				value = 0.0;					// canonical zero
			}
			return formatText("%.5f", value);
		}
		//---------------------------------------------------------
		String fmtEase(Ease const & ease)
		{
			if (ease.mode == Ease::LIN)
			{
				return String();
			}
			if (ease.mode == Ease::HOLD)
			{
				return String(" hold");
			}
			return formatText(" ease %.4f %.4f %.4f %.4f", ease.ox, ease.oy,
				ease.ix, ease.iy);
		}
		//---------------------------------------------------------
		String jsonStr(JsonValue const * value)
		{
			if (value == nullptr || value->isNull())
			{
				return String("None");
			}
			if (value->isString())
			{
				return value->asString();
			}
			if (value->isBool())
			{
				return value->asBool() ? String("True") : String("False");
			}
			if (value->isNumber())
			{
				double number = value->asNumber();
				if (number == std::floor(number) && std::fabs(number) < 1e15)
				{
					return formatText("%lld",
						static_cast<long long>(number));
				}
				return formatText("%.17g", number);
			}
			return String();
		}

		//=========================================================
		// JSON access
		//=========================================================
		//---------------------------------------------------------
		JsonValue const * member(JsonValue const * object, char const * key)
		{
			if (object == nullptr || !object->isObject())
			{
				return nullptr;
			}
			JsonValue const & found = object->get(key);
			return found.isNull() ? nullptr : &found;
		}
		//---------------------------------------------------------
		bool truthy(JsonValue const * value)
		{
			if (value == nullptr || value->isNull())
			{
				return false;
			}
			switch (value->getType())
			{
			case JsonValue::Type::Bool:		return value->asBool();
			case JsonValue::Type::Number:	return value->asNumber() != 0.0;
			case JsonValue::Type::String:	return !value->asString().empty();
			case JsonValue::Type::Array:
			case JsonValue::Type::Object:	return value->size() > 0;
			default:						return false;
			}
		}
		//---------------------------------------------------------
		double numberOr(JsonValue const * object, char const * key,
			double fallback)
		{
			JsonValue const * found = member(object, key);
			if (found == nullptr)
			{
				return fallback;
			}
			if (found->isBool())
			{
				return found->asBool() ? 1.0 : 0.0;
			}
			return found->asNumber(fallback);
		}
		//---------------------------------------------------------
		int intOr(JsonValue const * object, char const * key, int fallback)
		{
			JsonValue const * found = member(object, key);
			if (found == nullptr)
			{
				return fallback;
			}
			if (found->isBool())
			{
				return found->asBool() ? 1 : 0;
			}
			return static_cast<int>(found->asNumber(
				static_cast<double>(fallback)));
		}
		//---------------------------------------------------------
		std::vector<double> asList(JsonValue const * value)
		{
			std::vector<double> out;
			if (value == nullptr)
			{
				return out;
			}
			if (value->isNumber() || value->isBool())
			{
				out.push_back(value->isBool()
					? (value->asBool() ? 1.0 : 0.0) : value->asNumber());
				return out;
			}
			if (value->isArray())
			{
				for (size_t index = 0; index < value->size(); ++index)
				{
					JsonValue const & element = value->at(index);
					out.push_back(element.isBool()
						? (element.asBool() ? 1.0 : 0.0) : element.asNumber());
				}
			}
			return out;
		}

		//=========================================================
		// math helpers
		//=========================================================
		//---------------------------------------------------------
		//! @brief the exact two-dimensional magnitude, spelled out on purpose.
		//! @remarks This is DELIBERATELY not `std::hypot`. The platform
		//! function is only required to be faithful, so its last bit varies
		//! per libm and per architecture, and it disagrees with a correctly
		//! rounded magnitude on a sizeable fraction of inputs. The cook's
		//! lengths feed segment counts through a `ceil()`, so one wrong bit
		//! changes a whole contour's VERTEX COUNT - not merely its last printed
		//! decimal - and the same source document would then cook to different
		//! bytes on two machines. The formulation below is correctly rounded
		//! and therefore machine-independent: exact scaling by a power of two,
		//! an error-free product per term (fma), an error-free running sum,
		//! then one differential correction. It reproduces the algorithm the
		//! committed `.oanim` assets were cooked with; swapping in the platform
		//! `hypot` breaks the byte-identity gate in VectorAnimCookTests.
		double pyHypot(double x, double y)
		{
			double coordinates[2] = { std::fabs(x), std::fabs(y) };
			if (std::isnan(coordinates[0]) || std::isnan(coordinates[1]))
			{
				return std::numeric_limits<double>::quiet_NaN();
			}
			double max = std::max(coordinates[0], coordinates[1]);
			if (std::isinf(max))
			{
				return max;
			}
			if (max == 0.0)
			{
				return max;
			}
			int maxExponent = 0;
			std::frexp(max, &maxExponent);
			if (maxExponent < -1023)
			{
				// a subnormal magnitude: lift the whole vector into the normal
				// range, solve there, and scale the answer back
				double tiny = std::numeric_limits<double>::min();
				return tiny * pyHypot(coordinates[0] / tiny,
					coordinates[1] / tiny);
			}
			double scale = std::ldexp(1.0, -maxExponent);
			double csum = 1.0;
			double frac1 = 0.0;
			double frac2 = 0.0;
			for (int index = 0; index < 2; ++index)
			{
				double value = coordinates[index] * scale;	// lossless
				double product = value * value;
				double productLow = std::fma(value, value, -product);
				double sum = csum + product;				// |csum| >= |product|
				double sumLow = (csum - sum) + product;
				csum = sum;
				frac1 += productLow;
				frac2 += sumLow;
			}
			double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
			double product = -h * h;
			double productLow = std::fma(-h, h, -product);
			double sum = csum + product;
			double sumLow = (csum - sum) + product;
			csum = sum;
			frac1 += productLow;
			frac2 += sumLow;
			double residual = csum - 1.0 + (frac1 + frac2);
			h += residual / (2.0 * h);						// differential fix
			return h / scale;
		}
		//---------------------------------------------------------
		//! @brief the exact sum of a value run - a compensated accumulation,
		//! deliberately, not a plain running total.
		//! @remarks The sums this replaces are centroids, and a centroid moves
		//! every vertex of the shape built from it. A naive left-to-right total
		//! drifts by an amount that depends on the value order, which is
		//! exactly the kind of difference that lands a printed coordinate on
		//! the other side of a decimal. This reproduces the accumulation the
		//! committed assets were cooked with.
		double pySum(std::vector<double> const & values)
		{
			if (values.empty())
			{
				return 0.0;
			}
			double total = values[0];
			double compensation = 0.0;
			for (size_t index = 1; index < values.size(); ++index)
			{
				double value = values[index];
				double stepped = total + value;
				if (std::fabs(total) >= std::fabs(value))
				{
					compensation += (total - stepped) + value;
				}
				else
				{
					compensation += (value - stepped) + total;
				}
				total = stepped;
			}
			return total + compensation;
		}
		//---------------------------------------------------------
		double bezierEase(double ox, double oy, double ix, double iy, double u)
		{
			if (u <= 0.0)
			{
				return 0.0;
			}
			if (u >= 1.0)
			{
				return 1.0;
			}
			ox = std::min(std::max(ox, 0.0), 1.0);
			ix = std::min(std::max(ix, 0.0), 1.0);
			double low = 0.0;
			double high = 1.0;
			for (int step = 0; step < 48; ++step)
			{
				double mid = (low + high) * 0.5;
				double mt = 1.0 - mid;
				double at = 3 * mt * mt * mid * ox + 3 * mt * mid * mid * ix +
					mid * mid * mid;
				if (at < u)
				{
					low = mid;
				}
				else
				{
					high = mid;
				}
			}
			double t = (low + high) * 0.5;
			double mt = 1.0 - t;
			return 3 * mt * mt * t * oy + 3 * mt * t * t * iy + t * t * t;
		}
		//---------------------------------------------------------
		P2 cubicPoint(P2 const & p0, P2 const & c1, P2 const & c2,
			P2 const & p3, double t)
		{
			double mt = 1.0 - t;
			double a = mt * mt * mt;
			double b = 3 * mt * mt * t;
			double c = 3 * mt * t * t;
			double d = t * t * t;
			return P2(a * p0.x + b * c1.x + c * c2.x + d * p3.x,
				a * p0.y + b * c1.y + c * c2.y + d * p3.y);
		}
		//---------------------------------------------------------
		double distToSegment(P2 const & p, P2 const & a, P2 const & b)
		{
			double dx = b.x - a.x;
			double dy = b.y - a.y;
			double lengthSq = dx * dx + dy * dy;
			if (lengthSq <= EPS * EPS)
			{
				return pyHypot(p.x - a.x, p.y - a.y);
			}
			double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSq;
			t = std::min(std::max(t, 0.0), 1.0);
			return pyHypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
		}
		//---------------------------------------------------------
		double controlTurnAngle(P2 const & p0, P2 const & c1, P2 const & c2,
			P2 const & p3)
		{
			P2 const points[4] = { p0, c1, c2, p3 };
			double total = 0.0;
			for (int index = 0; index < 2; ++index)
			{
				double ax = points[index + 1].x - points[index].x;
				double ay = points[index + 1].y - points[index].y;
				double bx = points[index + 2].x - points[index + 1].x;
				double by = points[index + 2].y - points[index + 1].y;
				double la = pyHypot(ax, ay);
				double lb = pyHypot(bx, by);
				if (la <= EPS || lb <= EPS)
				{
					continue;
				}
				double cosang = (ax * bx + ay * by) / (la * lb);
				total += std::acos(std::min(std::max(cosang, -1.0), 1.0));
			}
			return total;
		}
		//---------------------------------------------------------
		bool pointInPolygon(P2 const & p, std::vector<P2> const & poly)
		{
			bool inside = false;
			size_t n = poly.size();
			if (n == 0)
			{
				return false;
			}
			size_t j = n - 1;
			for (size_t k = 0; k < n; ++k)
			{
				double xi = poly[k].x;
				double yi = poly[k].y;
				double xj = poly[j].x;
				double yj = poly[j].y;
				if ((yi > p.y) != (yj > p.y))
				{
					double cross = (xj - xi) * (p.y - yi) / (yj - yi) + xi;
					if (p.x < cross)
					{
						inside = !inside;
					}
				}
				j = k;
			}
			return inside;
		}
		//---------------------------------------------------------
		double polygonArea(std::vector<P2> const & poly)
		{
			double area = 0.0;
			size_t n = poly.size();
			for (size_t k = 0; k < n; ++k)
			{
				P2 const & first = poly[k];
				P2 const & second = poly[(k + 1) % n];
				area += first.x * second.y - second.x * first.y;
			}
			return area * 0.5;
		}

		//=========================================================
		// source properties
		//=========================================================
		//---------------------------------------------------------
		bool hasExpression(JsonValue const * prop)
		{
			if (prop == nullptr || !prop->isObject())
			{
				return false;
			}
			return prop->get("x").isString();
		}
		//---------------------------------------------------------
		bool propKeys(JsonValue const * prop, int dim,
			std::vector<PropKey> & out)
		{
			out.clear();
			JsonValue const * raw = member(prop, "k");
			if (raw == nullptr || !raw->isArray())
			{
				return true;			// python: an empty key list
			}
			size_t count = raw->size();
			for (size_t index = 0; index < count; ++index)
			{
				JsonValue const & entry = raw->at(index);
				if (!entry.isObject())
				{
					out.clear();
					return false;
				}
				JsonValue const * value = member(&entry, "s");
				if (value == nullptr && index > 0)
				{
					value = member(&raw->at(index - 1), "e");
				}
				if (value == nullptr)
				{
					out.clear();
					return false;
				}
				PropKey key;
				key.t = numberOr(&entry, "t", 0.0);
				key.h = intOr(&entry, "h", 0) == 1;
				key.easeOut = member(&entry, "o");
				key.easeIn = member(&entry, "i");
				key.spatialIn = member(&entry, "ti");
				key.spatialOut = member(&entry, "to");
				std::vector<double> values = asList(value);
				if (static_cast<int>(values.size()) > dim)
				{
					values.resize(static_cast<size_t>(dim));
				}
				while (static_cast<int>(values.size()) < dim)
				{
					values.push_back(values.empty() ? 0.0 : values.back());
				}
				key.s = values;
				out.push_back(key);
			}
			std::stable_sort(out.begin(), out.end(),
				[](PropKey const & a, PropKey const & b)
				{ return a.t < b.t; });
			return true;
		}
		//---------------------------------------------------------
		bool isAnimated(JsonValue const * prop, int dim)
		{
			if (prop == nullptr || !prop->isObject())
			{
				return false;
			}
			if (intOr(prop, "a", 0) != 1)
			{
				return false;
			}
			std::vector<PropKey> keys;
			if (!propKeys(prop, dim, keys) || keys.size() < 2)
			{
				return false;
			}
			for (PropKey const & key : keys)
			{
				if (key.s != keys[0].s)
				{
					return true;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		std::vector<double> staticValue(JsonValue const * prop, int dim,
			std::vector<double> const & fallback)
		{
			if (prop == nullptr)
			{
				return fallback;
			}
			if (!prop->isObject())
			{
				std::vector<double> values = asList(prop);
				if (static_cast<int>(values.size()) > dim)
				{
					values.resize(static_cast<size_t>(dim));
				}
				for (int extra = 0; extra < dim - 1; ++extra)
				{
					values.push_back(0.0);
				}
				return values;
			}
			if (intOr(prop, "a", 0) == 1)
			{
				std::vector<PropKey> keys;
				if (!propKeys(prop, dim, keys) || keys.empty())
				{
					return fallback;
				}
				return keys[0].s;
			}
			JsonValue const * raw = member(prop, "k");
			std::vector<double> values = raw != nullptr
				? asList(raw) : fallback;
			if (static_cast<int>(values.size()) > dim)
			{
				values.resize(static_cast<size_t>(dim));
			}
			while (static_cast<int>(values.size()) < dim)
			{
				values.push_back(values.empty() ? 0.0 : values.back());
			}
			return values;
		}
		//---------------------------------------------------------
		void easeComponents(JsonValue const * tangent, int dim, double fallback,
			std::vector<P2> & out)
		{
			out.assign(static_cast<size_t>(dim), P2(fallback, fallback));
			if (tangent == nullptr || !tangent->isObject())
			{
				return;
			}
			JsonValue const * xValue = member(tangent, "x");
			JsonValue const * yValue = member(tangent, "y");
			std::vector<double> xs = xValue != nullptr
				? asList(xValue) : std::vector<double>(1, fallback);
			std::vector<double> ys = yValue != nullptr
				? asList(yValue) : std::vector<double>(1, fallback);
			for (int d = 0; d < dim; ++d)
			{
				double x = fallback;
				double y = fallback;
				if (static_cast<size_t>(d) < xs.size())
				{
					x = xs[static_cast<size_t>(d)];
				}
				else if (!xs.empty())
				{
					x = xs.back();
				}
				if (static_cast<size_t>(d) < ys.size())
				{
					y = ys[static_cast<size_t>(d)];
				}
				else if (!ys.empty())
				{
					y = ys.back();
				}
				out[static_cast<size_t>(d)] = P2(x, y);
			}
		}
		//---------------------------------------------------------
		bool segmentEase(bool hold, JsonValue const * easeOut,
			JsonValue const * easeIn, int dim, Ease & out)
		{
			out = Ease();
			if (hold)
			{
				out.mode = Ease::HOLD;
				return true;
			}
			std::vector<P2> outPairs;
			std::vector<P2> inPairs;
			easeComponents(easeOut, dim, 1.0 / 3.0, outPairs);
			easeComponents(easeIn, dim, 2.0 / 3.0, inPairs);
			for (P2 const & pair : outPairs)
			{
				if (pair != outPairs[0])
				{
					return false;
				}
			}
			for (P2 const & pair : inPairs)
			{
				if (pair != inPairs[0])
				{
					return false;
				}
			}
			double ox = outPairs[0].x;
			double oy = outPairs[0].y;
			double ix = inPairs[0].x;
			double iy = inPairs[0].y;
			if (std::fabs(ox - oy) < EPS && std::fabs(ix - iy) < EPS)
			{
				out.mode = Ease::LIN;
				return true;
			}
			out.mode = Ease::BEZIER;
			out.ox = ox;
			out.oy = oy;
			out.ix = ix;
			out.iy = iy;
			return true;
		}
		//---------------------------------------------------------
		//! is a spatial tangent present and non-zero
		static bool tangentActive(JsonValue const * tangent)
		{
			if (!truthy(tangent))
			{
				return false;
			}
			std::vector<double> values = asList(tangent);
			for (double value : values)
			{
				if (std::fabs(value) > EPS)
				{
					return true;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		bool hasSpatialTangents(std::vector<PropKey> const & keys)
		{
			for (PropKey const & key : keys)
			{
				if (tangentActive(key.spatialIn) ||
					tangentActive(key.spatialOut))
				{
					return true;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		std::vector<double> sampleKeys(std::vector<PropKey> const & keys,
			double frame, int dim)
		{
			if (keys.empty())
			{
				return std::vector<double>(static_cast<size_t>(dim), 0.0);
			}
			if (frame <= keys.front().t)
			{
				return keys.front().s;
			}
			if (frame >= keys.back().t)
			{
				return keys.back().s;
			}
			for (size_t index = 0; index + 1 < keys.size(); ++index)
			{
				PropKey const & k0 = keys[index];
				PropKey const & k1 = keys[index + 1];
				bool inSegment = (k0.t <= frame && frame < k1.t) ||
					(index == keys.size() - 2 && frame <= k1.t);
				if (!inSegment)
				{
					continue;
				}
				if (frame >= k1.t)
				{
					return k1.s;
				}
				if (k0.h)
				{
					return k0.s;
				}
				double u = (frame - k0.t) / (k1.t - k0.t);
				std::vector<P2> outPairs;
				std::vector<P2> inPairs;
				easeComponents(k0.easeOut, dim, 1.0 / 3.0, outPairs);
				easeComponents(k0.easeIn, dim, 2.0 / 3.0, inPairs);
				std::vector<double> eased;
				eased.reserve(static_cast<size_t>(dim));
				for (int d = 0; d < dim; ++d)
				{
					eased.push_back(bezierEase(outPairs[d].x, outPairs[d].y,
						inPairs[d].x, inPairs[d].y, u));
				}
				bool spatial = dim == 2 && (tangentActive(k0.spatialOut) ||
					tangentActive(k0.spatialIn));
				if (spatial)
				{
					P2 p0(k0.s[0], k0.s[1]);
					P2 p3(k1.s[0], k1.s[1]);
					std::vector<double> to = asList(k0.spatialOut);
					std::vector<double> ti = asList(k0.spatialIn);
					while (to.size() < 2) { to.push_back(0.0); }
					while (ti.size() < 2) { ti.push_back(0.0); }
					P2 c1(p0.x + to[0], p0.y + to[1]);
					P2 c2(p3.x + ti[0], p3.y + ti[1]);
					P2 point = cubicPoint(p0, c1, c2, p3, eased[0]);
					std::vector<double> result;
					result.push_back(point.x);
					result.push_back(point.y);
					return result;
				}
				std::vector<double> result;
				result.reserve(static_cast<size_t>(dim));
				for (int d = 0; d < dim; ++d)
				{
					result.push_back(k0.s[d] + eased[d] * (k1.s[d] - k0.s[d]));
				}
				return result;
			}
			return keys.back().s;
		}
		//---------------------------------------------------------
		std::vector<double> sampleProp(JsonValue const * prop, int dim,
			std::vector<double> const & fallback, double frame)
		{
			if (isAnimated(prop, dim))
			{
				std::vector<PropKey> keys;
				propKeys(prop, dim, keys);
				return sampleKeys(keys, frame, dim);
			}
			return staticValue(prop, dim, fallback);
		}
		//---------------------------------------------------------
		std::vector<double> samplePosition(JsonValue const * prop, double frame,
			double defaultX, double defaultY)
		{
			if (prop != nullptr && prop->isObject() &&
				truthy(member(prop, "s")))
			{
				std::vector<double> out;
				out.push_back(sampleProp(member(prop, "x"), 1,
					std::vector<double>(1, defaultX), frame)[0]);
				out.push_back(sampleProp(member(prop, "y"), 1,
					std::vector<double>(1, defaultY), frame)[0]);
				return out;
			}
			std::vector<double> fallback;
			fallback.push_back(defaultX);
			fallback.push_back(defaultY);
			return sampleProp(prop, 2, fallback, frame);
		}

		//=========================================================
		// bezier path construction
		//=========================================================
		//---------------------------------------------------------
		//! read a list-of-pairs member into a point list
		static void readPointList(JsonValue const * object, char const * key,
			std::vector<P2> & out)
		{
			out.clear();
			JsonValue const * list = member(object, key);
			if (list == nullptr || !list->isArray())
			{
				return;
			}
			for (size_t index = 0; index < list->size(); ++index)
			{
				JsonValue const & point = list->at(index);
				out.push_back(P2(point.at(0).asNumber(),
					point.at(1).asNumber()));
			}
		}
		//---------------------------------------------------------
		BezPath pathFromLottie(JsonValue const * shapeValue)
		{
			BezPath path;
			readPointList(shapeValue, "v", path.v);
			readPointList(shapeValue, "i", path.i);
			readPointList(shapeValue, "o", path.o);
			while (path.i.size() < path.v.size())
			{
				path.i.push_back(P2(0.0, 0.0));
			}
			while (path.o.size() < path.v.size())
			{
				path.o.push_back(P2(0.0, 0.0));
			}
			JsonValue const * closed = member(shapeValue, "c");
			path.closed = closed == nullptr ? true : truthy(closed);
			return path;
		}
		//---------------------------------------------------------
		BezPath pathEllipse(std::vector<double> const & center,
			std::vector<double> const & size)
		{
			double cx = center[0];
			double cy = center[1];
			double rx = std::fabs(size[0]) * 0.5;
			double ry = std::fabs(size[1]) * 0.5;
			double kx = KAPPA * rx;
			double ky = KAPPA * ry;
			BezPath path;
			path.closed = true;
			path.v = { P2(cx, cy - ry), P2(cx + rx, cy), P2(cx, cy + ry),
				P2(cx - rx, cy) };
			path.o = { P2(kx, 0.0), P2(0.0, ky), P2(-kx, 0.0), P2(0.0, -ky) };
			path.i = { P2(-kx, 0.0), P2(0.0, -ky), P2(kx, 0.0), P2(0.0, ky) };
			return path;
		}
		//---------------------------------------------------------
		BezPath pathRect(std::vector<double> const & center,
			std::vector<double> const & size, double radius,
			bool roundedTopology)
		{
			double cx = center[0];
			double cy = center[1];
			double w2 = std::fabs(size[0]) * 0.5;
			double h2 = std::fabs(size[1]) * 0.5;
			double r = std::min(std::max(radius, 0.0), std::min(w2, h2));
			BezPath path;
			path.closed = true;
			if (!roundedTopology)
			{
				path.v = { P2(cx - w2, cy - h2), P2(cx + w2, cy - h2),
					P2(cx + w2, cy + h2), P2(cx - w2, cy + h2) };
				path.o.assign(4, P2(0.0, 0.0));
				path.i.assign(4, P2(0.0, 0.0));
				return path;
			}
			double k = KAPPA * r;
			path.v = {
				P2(cx + w2 - r, cy - h2), P2(cx + w2, cy - h2 + r),
				P2(cx + w2, cy + h2 - r), P2(cx + w2 - r, cy + h2),
				P2(cx - w2 + r, cy + h2), P2(cx - w2, cy + h2 - r),
				P2(cx - w2, cy - h2 + r), P2(cx - w2 + r, cy - h2) };
			path.o = {
				P2(k, 0.0), P2(0.0, 0.0), P2(0.0, k), P2(0.0, 0.0),
				P2(-k, 0.0), P2(0.0, 0.0), P2(0.0, -k), P2(0.0, 0.0) };
			path.i = {
				P2(0.0, 0.0), P2(0.0, -k), P2(0.0, 0.0), P2(k, 0.0),
				P2(0.0, 0.0), P2(0.0, k), P2(0.0, 0.0), P2(-k, 0.0) };
			return path;
		}
		//---------------------------------------------------------
		BezPath pathPolystar(JsonValue const * item, double frame)
		{
			double rawPoints = sampleProp(member(item, "pt"), 1,
				std::vector<double>(1, 5.0), frame)[0];
			int points = std::max(2,
				static_cast<int>(std::nearbyint(rawPoints)));
			std::vector<double> center = sampleProp(member(item, "p"), 2,
				std::vector<double>(2, 0.0), frame);
			double degToRad = 3.141592653589793 / 180.0;
			double rotation = (sampleProp(member(item, "r"), 1,
				std::vector<double>(1, 0.0), frame)[0] - 90.0) * degToRad;
			double outer = std::fabs(sampleProp(member(item, "or"), 1,
				std::vector<double>(1, 0.0), frame)[0]);
			double outerRound = std::max(0.0, sampleProp(member(item, "os"), 1,
				std::vector<double>(1, 0.0), frame)[0]) / 100.0;
			bool isStar = intOr(item, "sy", 1) == 1;
			double inner = std::fabs(sampleProp(member(item, "ir"), 1,
				std::vector<double>(1, outer * 0.5), frame)[0]);
			double innerRound = std::max(0.0, sampleProp(member(item, "is"), 1,
				std::vector<double>(1, 0.0), frame)[0]) / 100.0;
			int count = isStar ? points * 2 : points;
			double step = (3.141592653589793 * 2.0) / count;
			double direction = intOr(item, "d", 1) == 3 ? -1.0 : 1.0;
			BezPath path;
			path.closed = true;
			std::vector<double> radii;
			std::vector<double> rounds;
			for (int index = 0; index < count; ++index)
			{
				bool useInner = isStar && index % 2 == 1;
				double radius = useInner ? inner : outer;
				double angle = rotation + direction * step * index;
				path.v.push_back(P2(center[0] + std::cos(angle) * radius,
					center[1] + std::sin(angle) * radius));
				radii.push_back(radius);
				rounds.push_back(useInner ? innerRound : outerRound);
			}
			// the roundness handles are tangent to the circumcircle; 0.47829
			// is the reference handle constant for the star/polygon primitive
			for (int index = 0; index < count; ++index)
			{
				double angle = rotation + direction * step * index;
				double handle = radii[static_cast<size_t>(index)] *
					rounds[static_cast<size_t>(index)] * 0.47829 * step;
				double tx = -std::sin(angle) * direction * handle;
				double ty = std::cos(angle) * direction * handle;
				path.i.push_back(P2(-tx, -ty));
				path.o.push_back(P2(tx, ty));
			}
			return path;
		}
		//---------------------------------------------------------
		BezPath transformPath(BezPath const & path, Affine const & affine)
		{
			BezPath out;
			out.closed = path.closed;
			out.v.reserve(path.v.size());
			for (P2 const & point : path.v)
			{
				out.v.push_back(P2(
					affine.a * point.x + affine.b * point.y + affine.tx,
					affine.c * point.x + affine.d * point.y + affine.ty));
			}
			out.i.reserve(path.i.size());
			for (P2 const & point : path.i)
			{
				out.i.push_back(P2(affine.a * point.x + affine.b * point.y,
					affine.c * point.x + affine.d * point.y));
			}
			out.o.reserve(path.o.size());
			for (P2 const & point : path.o)
			{
				out.o.push_back(P2(affine.a * point.x + affine.b * point.y,
					affine.c * point.x + affine.d * point.y));
			}
			return out;
		}
		//---------------------------------------------------------
		std::vector<Edge> pathEdges(BezPath const & path)
		{
			std::vector<Edge> edges;
			size_t n = path.v.size();
			edges.reserve(n);
			for (size_t j = 0; j < n; ++j)
			{
				size_t j2 = (j + 1) % n;
				Edge edge;
				if (j2 == 0 && !path.closed)
				{
					edge.p0 = path.v[j];
					edge.c1 = path.v[j];
					edge.c2 = path.v[0];
					edge.p3 = path.v[0];
				}
				else
				{
					edge.p0 = path.v[j];
					edge.c1 = P2(path.v[j].x + path.o[j].x,
						path.v[j].y + path.o[j].y);
					edge.c2 = P2(path.v[j2].x + path.i[j2].x,
						path.v[j2].y + path.i[j2].y);
					edge.p3 = path.v[j2];
				}
				edges.push_back(edge);
			}
			return edges;
		}
		//---------------------------------------------------------
		std::vector<int> edgeSegmentCounts(std::vector<BezPath> const & paths,
			double tol)
		{
			std::vector<int> counts;
			if (paths.empty())
			{
				return counts;
			}
			size_t n = paths[0].v.size();
			std::vector<std::vector<Edge> > edgesPerPath;
			edgesPerPath.reserve(paths.size());
			for (BezPath const & path : paths)
			{
				edgesPerPath.push_back(pathEdges(path));
			}
			for (size_t j = 0; j < n; ++j)
			{
				double deviation = 0.0;
				double angle = 0.0;
				for (std::vector<Edge> const & edges : edgesPerPath)
				{
					Edge const & edge = edges[j];
					deviation = std::max(deviation,
						std::max(distToSegment(edge.c1, edge.p0, edge.p3),
							distToSegment(edge.c2, edge.p0, edge.p3)));
					angle = std::max(angle, controlTurnAngle(edge.p0, edge.c1,
						edge.c2, edge.p3));
				}
				if (deviation <= EPS)
				{
					counts.push_back(MIN_EDGE_SEGMENTS);
					continue;
				}
				int need = static_cast<int>(std::ceil(
					std::sqrt(0.75 * deviation / std::max(tol, EPS))));
				need = std::max(need, static_cast<int>(std::ceil(
					angle / EDGE_MAX_SEGMENT_ANGLE)));
				counts.push_back(std::min(std::max(need, MIN_EDGE_SEGMENTS),
					MAX_EDGE_SEGMENTS));
			}
			return counts;
		}
		//---------------------------------------------------------
		std::vector<P2> flattenPath(BezPath const & path,
			std::vector<int> const & counts)
		{
			std::vector<Edge> edges = pathEdges(path);
			std::vector<P2> points;
			if (path.v.empty())
			{
				return points;
			}
			points.push_back(path.v[0]);
			for (size_t j = 0; j < edges.size(); ++j)
			{
				int segments = counts[j];
				for (int k = 1; k <= segments; ++k)
				{
					points.push_back(cubicPoint(edges[j].p0, edges[j].c1,
						edges[j].c2, edges[j].p3,
						static_cast<double>(k) / segments));
				}
			}
			points.pop_back();		// the last edge returns to the start
			return points;
		}
		//---------------------------------------------------------
		//! lerp two point runs, stopping at the shorter one (python's zip)
		static std::vector<P2> lerpPoints(std::vector<P2> const & a,
			std::vector<P2> const & b, double u)
		{
			std::vector<P2> out;
			size_t count = std::min(a.size(), b.size());
			out.reserve(count);
			for (size_t index = 0; index < count; ++index)
			{
				out.push_back(P2(a[index].x + u * (b[index].x - a[index].x),
					a[index].y + u * (b[index].y - a[index].y)));
			}
			return out;
		}
		//---------------------------------------------------------
		BezPath lerpPath(BezPath const & a, BezPath const & b, double u)
		{
			BezPath out;
			out.closed = a.closed;
			out.v = lerpPoints(a.v, b.v, u);
			out.i = lerpPoints(a.i, b.i, u);
			out.o = lerpPoints(a.o, b.o, u);
			return out;
		}
		//---------------------------------------------------------
		std::vector<PathKey> pathPropKeys(JsonValue const * prop)
		{
			std::vector<PathKey> keys;
			JsonValue const * raw = member(prop, "k");
			if (raw == nullptr || !raw->isArray())
			{
				return keys;
			}
			for (size_t index = 0; index < raw->size(); ++index)
			{
				JsonValue const & entry = raw->at(index);
				JsonValue const * value = member(&entry, "s");
				if (value != nullptr && value->isArray() && value->size() > 0 &&
					value->at(0).isObject())
				{
					value = &value->at(0);
				}
				if (value == nullptr || !value->isObject())
				{
					continue;
				}
				PathKey key;
				key.t = numberOr(&entry, "t", 0.0);
				key.raw = value;
				key.path = pathFromLottie(value);
				key.h = intOr(&entry, "h", 0) == 1;
				key.easeOut = member(&entry, "o");
				key.easeIn = member(&entry, "i");
				keys.push_back(key);
			}
			std::stable_sort(keys.begin(), keys.end(),
				[](PathKey const & a, PathKey const & b)
				{ return a.t < b.t; });
			return keys;
		}
		//---------------------------------------------------------
		bool isAnimatedPath(JsonValue const * prop)
		{
			if (prop == nullptr || !prop->isObject() ||
				intOr(prop, "a", 0) != 1)
			{
				return false;
			}
			std::vector<PathKey> keys = pathPropKeys(prop);
			if (keys.size() < 2)
			{
				return false;
			}
			for (PathKey const & key : keys)
			{
				if (!(key.path == keys[0].path))
				{
					return true;
				}
			}
			return false;
		}
		//---------------------------------------------------------
		//! the bezier of a path property that does not animate
		BezPath staticPathBez(JsonValue const * prop)
		{
			if (prop == nullptr || !prop->isObject())
			{
				return BezPath();
			}
			if (intOr(prop, "a", 0) == 1)
			{
				std::vector<PathKey> keys = pathPropKeys(prop);
				return keys.empty() ? BezPath() : keys[0].path;
			}
			return pathFromLottie(member(prop, "k"));
		}
		//---------------------------------------------------------
		BezPath samplePathKeys(std::vector<PathKey> const & keys, double frame)
		{
			if (keys.empty())
			{
				return BezPath();
			}
			if (frame <= keys.front().t)
			{
				return keys.front().path;
			}
			if (frame >= keys.back().t)
			{
				return keys.back().path;
			}
			for (size_t index = 0; index + 1 < keys.size(); ++index)
			{
				PathKey const & k0 = keys[index];
				PathKey const & k1 = keys[index + 1];
				if (!(k0.t <= frame && frame <= k1.t))
				{
					continue;
				}
				if (k0.h)
				{
					return k0.path;
				}
				double u = (frame - k0.t) / std::max(k1.t - k0.t, EPS);
				Ease ease;
				if (!segmentEase(false, k0.easeOut, k0.easeIn, 1, ease))
				{
					ease = Ease();
				}
				if (ease.mode == Ease::BEZIER)
				{
					u = bezierEase(ease.ox, ease.oy, ease.ix, ease.iy, u);
				}
				else if (ease.mode == Ease::HOLD)
				{
					return k0.path;
				}
				if (k0.path.v.size() != k1.path.v.size())
				{
					return k0.path;
				}
				return lerpPath(k0.path, k1.path, u);
			}
			return keys.back().path;
		}
		//---------------------------------------------------------
		bool edgeIsLinear(BezPath const & path, size_t index)
		{
			std::vector<Edge> edges = pathEdges(path);
			Edge const & edge = edges[index];
			double tolerance = std::max(1e-5,
				pyHypot(edge.p3.x - edge.p0.x, edge.p3.y - edge.p0.y) * 1e-5);
			return distToSegment(edge.c1, edge.p0, edge.p3) <= tolerance &&
				distToSegment(edge.c2, edge.p0, edge.p3) <= tolerance;
		}
		//---------------------------------------------------------
		BezPath roundPathCorners(BezPath const & path, double radius)
		{
			size_t count = path.v.size();
			if (count < 2)
			{
				return path;
			}
			radius = std::max(radius, 0.0);
			BezPath out;
			out.closed = path.closed;
			for (size_t index = 0; index < count; ++index)
			{
				P2 const & vertex = path.v[index];
				bool endpoint = !path.closed &&
					(index == 0 || index == count - 1);
				size_t previous = (index + count - 1) % count;
				size_t following = (index + 1) % count;
				bool roundable = !endpoint && edgeIsLinear(path, previous) &&
					edgeIsLinear(path, index);
				if (roundable && radius > EPS)
				{
					P2 const & before = path.v[previous];
					P2 const & after = path.v[following];
					double beforeLength = pyHypot(before.x - vertex.x,
						before.y - vertex.y);
					double afterLength = pyHypot(after.x - vertex.x,
						after.y - vertex.y);
					double distance = std::min(radius,
						std::min(beforeLength * 0.5, afterLength * 0.5));
					if (distance > EPS && beforeLength > EPS &&
						afterLength > EPS)
					{
						P2 first(
							vertex.x + (before.x - vertex.x) * distance /
								beforeLength,
							vertex.y + (before.y - vertex.y) * distance /
								beforeLength);
						P2 second(
							vertex.x + (after.x - vertex.x) * distance /
								afterLength,
							vertex.y + (after.y - vertex.y) * distance /
								afterLength);
						out.v.push_back(first);
						out.v.push_back(second);
						out.i.push_back(P2(0.0, 0.0));
						out.i.push_back(P2((vertex.x - second.x) * KAPPA,
							(vertex.y - second.y) * KAPPA));
						out.o.push_back(P2((vertex.x - first.x) * KAPPA,
							(vertex.y - first.y) * KAPPA));
						out.o.push_back(P2(0.0, 0.0));
						continue;
					}
				}
				if (endpoint)
				{
					out.v.push_back(vertex);
					out.i.push_back(path.i[index]);
					out.o.push_back(path.o[index]);
				}
				else
				{
					out.v.push_back(vertex);
					out.v.push_back(vertex);
					out.i.push_back(path.i[index]);
					out.i.push_back(P2(0.0, 0.0));
					out.o.push_back(P2(0.0, 0.0));
					out.o.push_back(path.o[index]);
				}
			}
			return out;
		}
		//---------------------------------------------------------
		BezPath puckerBloatPath(BezPath const & path, double rawAmount)
		{
			if (path.v.empty())
			{
				return path;
			}
			double amount = rawAmount / 100.0;
			std::vector<double> xs;
			std::vector<double> ys;
			xs.reserve(path.v.size());
			ys.reserve(path.v.size());
			for (P2 const & point : path.v)
			{
				xs.push_back(point.x);
				ys.push_back(point.y);
			}
			P2 center(pySum(xs) / path.v.size(), pySum(ys) / path.v.size());
			BezPath out;
			out.closed = path.closed;
			size_t count = std::min(path.v.size(),
				std::min(path.i.size(), path.o.size()));
			for (size_t index = 0; index < count; ++index)
			{
				P2 const & vertex = path.v[index];
				P2 const & inHandle = path.i[index];
				P2 const & outHandle = path.o[index];
				P2 moved(vertex.x + amount * (center.x - vertex.x),
					vertex.y + amount * (center.y - vertex.y));
				P2 absoluteIn(vertex.x + inHandle.x, vertex.y + inHandle.y);
				P2 absoluteOut(vertex.x + outHandle.x, vertex.y + outHandle.y);
				P2 movedIn(
					absoluteIn.x - amount * (center.x - absoluteIn.x),
					absoluteIn.y - amount * (center.y - absoluteIn.y));
				P2 movedOut(
					absoluteOut.x - amount * (center.x - absoluteOut.x),
					absoluteOut.y - amount * (center.y - absoluteOut.y));
				out.v.push_back(moved);
				out.i.push_back(P2(movedIn.x - moved.x, movedIn.y - moved.y));
				out.o.push_back(P2(movedOut.x - moved.x,
					movedOut.y - moved.y));
			}
			return out;
		}
		//---------------------------------------------------------
		double rectMaxRadius(JsonValue const * item)
		{
			JsonValue const * prop = member(item, "r");
			if (isAnimated(prop, 1))
			{
				std::vector<PropKey> keys;
				propKeys(prop, 1, keys);
				double best = 0.0;
				bool first = true;
				for (PropKey const & key : keys)
				{
					double value = std::fabs(key.s[0]);
					if (first || value > best)
					{
						best = value;
						first = false;
					}
				}
				return best;
			}
			return std::fabs(staticValue(prop, 1,
				std::vector<double>(1, 0.0))[0]);
		}
		//---------------------------------------------------------
		BezPath blockPathAt(String const & kind, JsonValue const * item,
			double frame)
		{
			if (kind == "sh")
			{
				JsonValue const * prop = member(item, "ks");
				if (isAnimatedPath(prop))
				{
					return samplePathKeys(pathPropKeys(prop), frame);
				}
				return staticPathBez(prop);
			}
			if (kind == "el")
			{
				std::vector<double> p = sampleProp(member(item, "p"), 2,
					std::vector<double>(2, 0.0), frame);
				std::vector<double> s = sampleProp(member(item, "s"), 2,
					std::vector<double>(2, 0.0), frame);
				return pathEllipse(p, s);
			}
			if (kind == "sr")
			{
				return pathPolystar(item, frame);
			}
			std::vector<double> p = sampleProp(member(item, "p"), 2,
				std::vector<double>(2, 0.0), frame);
			std::vector<double> s = sampleProp(member(item, "s"), 2,
				std::vector<double>(2, 0.0), frame);
			double r = sampleProp(member(item, "r"), 1,
				std::vector<double>(1, 0.0), frame)[0];
			bool rounded = rectMaxRadius(item) > EPS;
			return pathRect(p, s, r, rounded);
		}
		//---------------------------------------------------------
		BezPath blockPathWithModifiers(Block const & block, String const & kind,
			JsonValue const * item, double frame)
		{
			BezPath path = blockPathAt(kind, item, frame);
			for (JsonValue const * modifier : block.modifiers)
			{
				JsonValue const * type = member(modifier, "ty");
				String typeName = type != nullptr ? type->asString() : String();
				if (typeName == "rd")
				{
					double radius = sampleProp(member(modifier, "r"), 1,
						std::vector<double>(1, 0.0), frame)[0];
					path = roundPathCorners(path, radius);
				}
				else if (typeName == "pb")
				{
					double amount = sampleProp(member(modifier, "a"), 1,
						std::vector<double>(1, 0.0), frame)[0];
					path = puckerBloatPath(path, amount);
				}
			}
			return path;
		}

		//=========================================================
		// affines
		//=========================================================
		//---------------------------------------------------------
		Affine groupAffineAt(JsonValue const * tr, double frame)
		{
			std::vector<double> p = samplePosition(member(tr, "p"), frame,
				0.0, 0.0);
			std::vector<double> a = sampleProp(member(tr, "a"), 2,
				std::vector<double>(2, 0.0), frame);
			std::vector<double> s = sampleProp(member(tr, "s"), 2,
				std::vector<double>(2, 100.0), frame);
			double r = sampleProp(member(tr, "r"), 1,
				std::vector<double>(1, 0.0), frame)[0];
			double rad = r * (3.141592653589793 / 180.0);
			double cosR = std::cos(rad);
			double sinR = std::sin(rad);
			double sx = s[0] / 100.0;
			double sy = s[1] / 100.0;
			double la = cosR * sx;
			double lb = -sinR * sy;
			double lc = sinR * sx;
			double ld = cosR * sy;
			double tx = p[0] - (la * a[0] + lb * a[1]);
			double ty = p[1] - (lc * a[0] + ld * a[1]);
			return Affine(la, lb, lc, ld, tx, ty);
		}
		//---------------------------------------------------------
		Affine composeAffines(std::vector<Affine> const & affines)
		{
			Affine out;
			for (Affine const & next : affines)
			{
				Affine composed(
					out.a * next.a + out.b * next.c,
					out.a * next.b + out.b * next.d,
					out.c * next.a + out.d * next.c,
					out.c * next.b + out.d * next.d,
					out.a * next.tx + out.b * next.ty + out.tx,
					out.c * next.tx + out.d * next.ty + out.ty);
				out = composed;
			}
			return out;
		}
		//---------------------------------------------------------
		Affine blockAffineAt(Block const & block, double frame)
		{
			std::vector<Affine> affines;
			affines.reserve(block.affines.size());
			for (JsonValue const * transform : block.affines)
			{
				affines.push_back(groupAffineAt(transform, frame));
			}
			return composeAffines(affines);
		}

		//=========================================================
		// polyline utilities
		//=========================================================
		//---------------------------------------------------------
		void polylineChain(std::vector<P2> const & points, bool closed,
			std::vector<P2> & chain, std::vector<double> & lengths)
		{
			chain = points;
			if (closed && !chain.empty() && chain.back() != chain.front())
			{
				chain.push_back(chain.front());
			}
			lengths.assign(1, 0.0);
			for (size_t index = 0; index + 1 < chain.size(); ++index)
			{
				lengths.push_back(lengths.back() +
					pyHypot(chain[index + 1].x - chain[index].x,
						chain[index + 1].y - chain[index].y));
			}
		}
		//---------------------------------------------------------
		P2 pointOnChain(std::vector<P2> const & chain,
			std::vector<double> const & lengths, double distance)
		{
			if (chain.empty())
			{
				return P2(0.0, 0.0);
			}
			distance = std::min(std::max(distance, 0.0), lengths.back());
			for (size_t index = 0; index + 1 < chain.size(); ++index)
			{
				if (distance <= lengths[index + 1] + EPS)
				{
					double span = lengths[index + 1] - lengths[index];
					double u = span <= EPS ? 0.0
						: (distance - lengths[index]) / span;
					P2 const & first = chain[index];
					P2 const & second = chain[index + 1];
					return P2(first.x + (second.x - first.x) * u,
						first.y + (second.y - first.y) * u);
				}
			}
			return chain.back();
		}
		//---------------------------------------------------------
		std::vector<P2> sliceChain(std::vector<P2> const & chain,
			std::vector<double> const & lengths, double start, double end)
		{
			std::vector<P2> points;
			points.push_back(pointOnChain(chain, lengths, start));
			for (size_t index = 1; index + 1 < chain.size(); ++index)
			{
				if (start + EPS < lengths[index] && lengths[index] < end - EPS)
				{
					points.push_back(chain[index]);
				}
			}
			points.push_back(pointOnChain(chain, lengths, end));
			return points;
		}
		//---------------------------------------------------------
		std::vector<P2> resampleOpen(std::vector<P2> const & points, int count)
		{
			std::vector<P2> chain;
			std::vector<double> lengths;
			polylineChain(points, false, chain, lengths);
			if (chain.size() < 2 || lengths.back() <= EPS)
			{
				P2 anchor = chain.empty() ? P2(0.0, 0.0) : chain.front();
				return std::vector<P2>(static_cast<size_t>(count), anchor);
			}
			std::vector<P2> out;
			out.reserve(static_cast<size_t>(count));
			for (int index = 0; index < count; ++index)
			{
				out.push_back(pointOnChain(chain, lengths,
					lengths.back() * index / (count - 1)));
			}
			return out;
		}
		//---------------------------------------------------------
		std::vector<P2> resampleClosed(std::vector<P2> const & points,
			int count)
		{
			if (points.size() < 3 || count < 3)
			{
				return points;
			}
			std::vector<P2> chain = points;
			chain.push_back(points.front());
			std::vector<double> lengths(1, 0.0);
			for (size_t index = 0; index + 1 < chain.size(); ++index)
			{
				lengths.push_back(lengths.back() +
					pyHypot(chain[index + 1].x - chain[index].x,
						chain[index + 1].y - chain[index].y));
			}
			double total = lengths.back();
			if (total <= EPS)
			{
				return std::vector<P2>(static_cast<size_t>(count),
					points.front());
			}
			std::vector<P2> result;
			for (int sample = 0; sample < count; ++sample)
			{
				double distance = total * sample / count;
				for (size_t index = 0; index < points.size(); ++index)
				{
					if (distance <= lengths[index + 1] + EPS)
					{
						double span = std::max(
							lengths[index + 1] - lengths[index], EPS);
						double u = (distance - lengths[index]) / span;
						P2 const & a = chain[index];
						P2 const & b = chain[index + 1];
						result.push_back(P2(a.x + (b.x - a.x) * u,
							a.y + (b.y - a.y) * u));
						break;
					}
				}
			}
			return result;
		}
		//---------------------------------------------------------
		std::vector<P2> clipConvex(std::vector<P2> const & subject,
			std::vector<P2> const & clip)
		{
			if (subject.size() < 3 || clip.size() < 3)
			{
				return std::vector<P2>();
			}
			double sign = polygonArea(clip) >= 0.0 ? 1.0 : -1.0;
			auto inside = [&sign](P2 const & point, P2 const & a, P2 const & b)
			{
				return sign * ((b.x - a.x) * (point.y - a.y) -
					(b.y - a.y) * (point.x - a.x)) >= -EPS;
			};
			auto intersection = [](P2 const & p, P2 const & q, P2 const & a,
				P2 const & b)
			{
				double rx = q.x - p.x;
				double ry = q.y - p.y;
				double sx = b.x - a.x;
				double sy = b.y - a.y;
				double denominator = rx * sy - ry * sx;
				if (std::fabs(denominator) <= EPS)
				{
					return q;
				}
				double t = ((a.x - p.x) * sy - (a.y - p.y) * sx) / denominator;
				return P2(p.x + t * rx, p.y + t * ry);
			};
			std::vector<P2> output = subject;
			for (size_t index = 0; index < clip.size(); ++index)
			{
				P2 const & a = clip[index];
				P2 const & b = clip[(index + 1) % clip.size()];
				std::vector<P2> source;
				source.swap(output);
				if (source.empty())
				{
					break;
				}
				P2 previous = source.back();
				bool previousInside = inside(previous, a, b);
				for (P2 const & current : source)
				{
					bool currentInside = inside(current, a, b);
					if (currentInside)
					{
						if (!previousInside)
						{
							output.push_back(intersection(previous, current,
								a, b));
						}
						output.push_back(current);
					}
					else if (previousInside)
					{
						output.push_back(intersection(previous, current, a, b));
					}
					previous = current;
					previousInside = currentInside;
				}
			}
			return output;
		}

		//=========================================================
		// name helpers
		//=========================================================
		//---------------------------------------------------------
		//! python's whitespace class for str.strip() and \s
		static bool isPySpace(char c)
		{
			return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
				c == '\f' || c == '\v';
		}
		//---------------------------------------------------------
		String sanitizeName(String const & name, String const & fallback)
		{
			size_t begin = 0;
			size_t end = name.size();
			while (begin < end && isPySpace(name[begin]))
			{
				++begin;
			}
			while (end > begin && isPySpace(name[end - 1]))
			{
				--end;
			}
			String out;
			bool inRun = false;
			for (size_t index = begin; index < end; ++index)
			{
				char c = name[index];
				if (isPySpace(c))
				{
					if (!inRun)
					{
						out.push_back('_');
						inRun = true;
					}
					continue;
				}
				inRun = false;
				out.push_back(c == '#' ? '_' : c);
			}
			return out.empty() ? fallback : out;
		}
		//---------------------------------------------------------
		String sanitizeName(JsonValue const * name, String const & fallback)
		{
			// python's `str(name or "")`: a falsy value becomes the empty
			// string, so an absent, null, empty or zero name takes the fallback
			if (!truthy(name))
			{
				return sanitizeName(String(), fallback);
			}
			if (name->isString())
			{
				return sanitizeName(name->asString(), fallback);
			}
			return sanitizeName(jsonStr(name), fallback);
		}
	}
}
