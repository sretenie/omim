#include "map/styled_point.hpp"

#include "base/logging.hpp"

namespace
{
char const * kSupportedColors[] = {"placemark-red",    "placemark-blue",  "placemark-purple",
                                   "placemark-yellow", "placemark-pink",  "placemark-brown",
                                   "placemark-green",  "placemark-orange",
                                   "3", "4", "5", "6", "7", "8", "9", "10", "10+", "20+",
                                   "30+", "40+", "50+", "100+", "500+", "1000+",
                                   "bell", "book", "foto", "video",
                                   "166", "167", "168", "169", "170", "172", "173", "174", "175", "176",
                                   "177", "178", "179", "180", "181", "182", "183", "184", "185", "186",
                                   "187", "188", "189", "190", "191", "192", "253", "253", "254", "255",
                                   "256", "257", "259", "260", "261", "262", "263", "264", "265", "266",
                                   "267", "268", "269", "270", "271", "272", "273", "274", "277", "best"};
}

namespace style
{

StyledPoint::StyledPoint(m2::PointD const & ptOrg, UserMarkContainer * container)
  : UserMark(ptOrg, container)
{
}

m2::PointD const & StyledPoint::GetPixelOffset() const
{
  static m2::PointD const s_centre(0.0, 0.0);
  static m2::PointD const s_offset(0.0, 3.0);

  return GetStyle().empty() ? s_centre : s_offset;
}

string GetSupportedStyle(string const & s, string const & context, string const & fallback)
{
  if (s.empty())
    return fallback;

  for (size_t i = 0; i < ARRAY_SIZE(kSupportedColors); ++i)
  {
    if (s == kSupportedColors[i])
      return s;
  }

  // Not recognized symbols are replaced with default one
  LOG(LWARNING, ("Icon", s, "for point", context, "is not supported"));
  return fallback;
}

string GetDefaultStyle() { return kSupportedColors[0]; }
}  // namespace style
