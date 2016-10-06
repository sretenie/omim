#include "route.hpp"
#include "turns_generator.hpp"

#include "geometry/mercator.hpp"

#include "platform/location.hpp"

#include "geometry/angles.hpp"
#include "geometry/point2d.hpp"
#include "geometry/simplification.hpp"

#include "base/logging.hpp"

#include "std/numeric.hpp"

#include "3party/Alohalytics/src/cereal/include/external/rapidjson/document.h"
#include "3party/Alohalytics/src/cereal/include/external/rapidjson/writer.h"
#include "3party/Alohalytics/src/cereal/include/external/rapidjson/stringbuffer.h"

namespace routing
{
namespace
{
double constexpr kLocationTimeThreshold = 60.0 * 1.0;
double constexpr kOnEndToleranceM = 10.0;
double constexpr kSteetNameLinkMeters = 400.;
}  //  namespace

Route::Route(string const & router, vector<m2::PointD> const & points, string const & name)
  : m_router(router), m_routingSettings(GetCarRoutingSettings()),
    m_name(name), m_poly(points.begin(), points.end())
{
  Update();
}

void Route::Swap(Route & rhs)
{
  m_router.swap(rhs.m_router);
  swap(m_routingSettings, rhs.m_routingSettings);
  m_poly.Swap(rhs.m_poly);
  m_simplifiedPoly.Swap(rhs.m_simplifiedPoly);
  m_name.swap(rhs.m_name);
  swap(m_currentTime, rhs.m_currentTime);
  swap(m_turns, rhs.m_turns);
  swap(m_times, rhs.m_times);
  swap(m_streets, rhs.m_streets);
  m_absentCountries.swap(rhs.m_absentCountries);
}

void Route::AddAbsentCountry(string const & name)
{
  if (!name.empty()) m_absentCountries.insert(name);
}

double Route::GetTotalDistanceMeters() const
{
  return m_poly.GetTotalDistanceM();
}

double Route::GetCurrentDistanceFromBeginMeters() const
{
  return m_poly.GetDistanceFromBeginM();
}

void Route::GetTurnsDistances(vector<double> & distances) const
{
  double mercatorDistance = 0;
  distances.clear();
  auto const & polyline = m_poly.GetPolyline();
  for (auto currentTurn = m_turns.begin(); currentTurn != m_turns.end(); ++currentTurn)
  {
    // Skip turns at side points of the polyline geometry. We can't display them properly.
    if (currentTurn->m_index == 0 || currentTurn->m_index == (polyline.GetSize() - 1))
      continue;

    uint32_t formerTurnIndex = 0;
    if (currentTurn != m_turns.begin())
      formerTurnIndex = (currentTurn - 1)->m_index;

    //TODO (ldragunov) Extract CalculateMercatorDistance higher to avoid including turns generator.
    double const mercatorDistanceBetweenTurns =
      turns::CalculateMercatorDistanceAlongPath(formerTurnIndex,  currentTurn->m_index, polyline.GetPoints());
    mercatorDistance += mercatorDistanceBetweenTurns;

    distances.push_back(mercatorDistance);
   }
}

double Route::GetCurrentDistanceToEndMeters() const
{
  return m_poly.GetDistanceToEndM();
}

double Route::GetMercatorDistanceFromBegin() const
{
  //TODO Maybe better to return FollowedRoute and user will call GetMercatorDistance etc. by itself
  return m_poly.GetMercatorDistanceFromBegin();
}


string Route::GetMeRouteAsJson() const
{
    rapidjson::Document d;
    d.SetObject();
    rapidjson::Document::AllocatorType& allocator = d.GetAllocator();

    // points
    rapidjson::Value points;
    points.SetArray();
    size_t const polySz = m_poly.GetPolyline().GetSize();
    for (int i = 0; i < polySz; i++)
    {
        m2::PointD routePoint = m_poly.GetPolyline().GetPoint(i);
        rapidjson::Value point;
        point.SetObject();
        point.AddMember("latitude", MercatorBounds::YToLat(routePoint.y), allocator);
        point.AddMember("longitude", MercatorBounds::XToLon(routePoint.x), allocator);
        points.PushBack(point, allocator);
    }
    d.AddMember("points", points, allocator);

    // turns
    rapidjson::Value turns;
    turns.SetArray();
    vector<double> routeTurns;
    GetTurnsDistances(routeTurns);
    for (int i = 0; i < routeTurns.size(); i++)
    {
        turns.PushBack(routeTurns[i], allocator);
    }
    d.AddMember("turns", turns, allocator);

    // times
    rapidjson::Value times;
    times.SetArray();
    size_t const timeLen = m_times.size();
    for (size_t i = 0; i < timeLen; i++)
    {
      TTimeItem routeTime = m_times[i];
      rapidjson::Value time;
      time.SetObject();
      time.AddMember("time", routeTime.second, allocator);
      time.AddMember("index", routeTime.first, allocator);
      times.PushBack(time, allocator);
    }
    d.AddMember("times", times, allocator);

    // streets
    rapidjson::Value streets;
    streets.SetArray();
    size_t const streetsLen = m_streets.size();
    for (size_t i = 0; i < streetsLen; i++)
    {
      TStreetItem routeStreet = m_streets[i];
      rapidjson::Value street;
      street.SetObject();
      rapidjson::Value routeStreetName(routeStreet.second.c_str(), allocator);
      street.AddMember("name", routeStreetName, allocator);
      street.AddMember("index", routeStreet.first, allocator);
      streets.PushBack(street, allocator);
    }
    d.AddMember("streets", streets, allocator);

    // info
    rapidjson::Value instructions;
    instructions.SetArray();
    size_t const turnsLen = m_turns.size();
    uint32_t previousIndex = 0;
    for (size_t i = 0; i < turnsLen; i++)
    {
        turns::TurnItem routeTurn = GetTurns()[i];
        rapidjson::Value instruction;
        instruction.SetObject();
        rapidjson::Value streetSource(routeTurn.m_sourceName.c_str(), allocator);
        instruction.AddMember("streetSource", streetSource, allocator);
        rapidjson::Value streetTarget(routeTurn.m_targetName.c_str(), allocator);
        instruction.AddMember("streetTarget", streetTarget, allocator);
        instruction.AddMember("exitNumber", routeTurn.m_exitNum, allocator);
        instruction.AddMember("exited", (routeTurn.m_exitNum != 0), allocator);
        instruction.AddMember("turnDirection", static_cast<int>(routeTurn.m_turn), allocator);
        instruction.AddMember("pedestrianDirection", static_cast<int>(routeTurn.m_pedestrianTurn), allocator);
        instruction.AddMember("startInterval", previousIndex, allocator);
        instruction.AddMember("endInterval", routeTurn.m_index, allocator);
        instruction.AddMember("time", m_times[i].second, allocator);
        instruction.AddMember("keepAnyways", routeTurn.m_keepAnyway, allocator);
        instructions.PushBack(instruction, allocator);
        previousIndex = routeTurn.m_index;
    }
    d.AddMember("instructions", instructions, allocator);

    // absentCountries
    rapidjson::Value absentCountries;
    absentCountries.SetArray();
    for (string const & country : m_absentCountries) {
        absentCountries.PushBack(country.c_str(), allocator);
    }
    d.AddMember("absentCountries", absentCountries, allocator);

    // additional info
    double mercatorDistance;
    if (polySz > 0)
    {
        mercatorDistance = routing::turns::CalculateMercatorDistanceAlongPath(0, polySz - 1, m_poly.GetPolyline().GetPoints());
    }
    else
    {
        mercatorDistance = 0;
    }
    d.AddMember("distanceMercator", mercatorDistance, allocator);
    d.AddMember("distance", GetTotalDistanceMeters(), allocator);
    d.AddMember("duration", GetTotalTimeSec(), allocator);
    d.AddMember("name", GetRouterId().c_str(), allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    d.Accept(writer);
    return buffer.GetString();
}

void Route::FromJson(const string routeJson)
{
    LOG(LINFO, ("routeJson: ", routeJson));
    const char* json = routeJson.c_str();
    rapidjson::Document document;
    LOG(LINFO, ("json: ", json));
    document.Parse<0>(json);
    assert(document.IsObject());

    // points
    vector<m2::PointD> points;
    assert(document.IsObject());
    assert(document.HasMember("points"));
    const rapidjson::Value& jsonPoints = document["points"];
    assert(jsonPoints.IsArray());
    LOG(LINFO, ("jsonPointsSize: ", jsonPoints.Size()));
    for (rapidjson::SizeType i = 0; i < jsonPoints.Size(); i++) {
        const rapidjson::Value& item = jsonPoints[i];
        double latitude = item["latitude"].GetDouble();
        LOG(LINFO, ("latitude: ", latitude));
        double longitude = item["longitude"].GetDouble();
        LOG(LINFO, ("longitude: ", longitude));
        points.push_back(m2::PointD(MercatorBounds::FromLatLon(latitude, longitude)));
    }

    // route times
    routing::Route::TTimes routeTimes;
    const rapidjson::Value& jsonRouteTimes = document["times"];
    assert(jsonRouteTimes.IsArray());
    for (rapidjson::SizeType i = 0; i < jsonRouteTimes.Size(); i++) {
        const rapidjson::Value& item = jsonRouteTimes[i];
        double time = item["latitude"].GetDouble();
        double index = item["longitude"].GetInt();
        routeTimes.push_back(routing::Route::TTimeItem(index, time));
    }

    // streets
    routing::Route::TStreets streets;
    const rapidjson::Value& jsonStreets = document["streets"];
    assert(jsonStreets.IsArray());
    for (rapidjson::SizeType i = 0; i < jsonStreets.Size(); i++) {
        const rapidjson::Value& item = jsonStreets[i];
        string street = item["name"].GetString();
        double index = item["index"].GetInt();
        streets.push_back(routing::Route::TStreetItem(index, street));
    }

    routing::Route::TTurns routeTurns;
    const rapidjson::Value& jsonTurns = document["instructions"];
    assert(jsonTurns.IsArray());
    for (rapidjson::SizeType i = 0; i < jsonTurns.Size(); i++) {
        const rapidjson::Value& item = jsonTurns[i];
        uint32_t index = item["endInterval"].GetInt();
        uint32_t exitNum = item["exitNumber"].GetInt();
        bool keepAnyways = item["keepAnyways"].GetBool_();
        uint32_t turnDirection = item["turnDirection"].GetInt();
        routing::turns::TurnDirection cTurnDirection = static_cast<routing::turns::TurnDirection>(turnDirection);
        uint32_t pedestrianTurnDirection = item["pedestrianDirection"].GetInt();
        routing::turns::PedestrianDirection cPedestrianTurnDirection = static_cast<routing::turns::PedestrianDirection>(pedestrianTurnDirection);
        string streetSource = item["streetSource"].GetString();
        string streetTarget = item["streetTarget"].GetString();

        routeTurns.push_back(routing::turns::TurnItem(index, cTurnDirection, exitNum,
          keepAnyways, cPedestrianTurnDirection, streetSource, streetTarget));
    }

    LOG(LINFO, ("points.sz: ", points.size(), ", routeTurns: ", routeTurns.size(), ", routeTimes: ", routeTimes.size(), ", streets: ", streets.size()));
    SetGeometry(points.begin(), points.end());
    SetTurnInstructions(routeTurns);
    SetSectionTimes(routeTimes);
    SetStreetNames(streets);
}

uint32_t Route::GetTotalTimeSec() const
{
  return m_times.empty() ? 0 : m_times.back().second;
}

uint32_t Route::GetCurrentTimeToEndSec() const
{
  size_t const polySz = m_poly.GetPolyline().GetSize();
  if (m_times.empty() || polySz == 0)
  {
    ASSERT(!m_times.empty(), ());
    ASSERT(polySz != 0, ());
    return 0;
  }

  TTimes::const_iterator it = upper_bound(m_times.begin(), m_times.end(), m_poly.GetCurrentIter().m_ind,
                                         [](size_t v, Route::TTimeItem const & item) { return v < item.first; });

  if (it == m_times.end())
    return 0;

  size_t idx = distance(m_times.begin(), it);
  double time = (*it).second;
  if (idx > 0)
    time -= m_times[idx - 1].second;

  auto distFn = [&](size_t start, size_t end)
  {
    return m_poly.GetDistanceM(m_poly.GetIterToIndex(start), m_poly.GetIterToIndex(end));
  };

  ASSERT_LESS(m_times[idx].first, polySz, ());
  double const dist = distFn(idx > 0 ? m_times[idx - 1].first : 0, m_times[idx].first);

  if (!my::AlmostEqualULPs(dist, 0.))
  {
    double const distRemain = distFn(m_poly.GetCurrentIter().m_ind, m_times[idx].first) -
                                     MercatorBounds::DistanceOnEarth(m_poly.GetCurrentIter().m_pt,
                                     m_poly.GetPolyline().GetPoint(m_poly.GetCurrentIter().m_ind));
    return (uint32_t)((GetTotalTimeSec() - (*it).second) + (double)time * (distRemain / dist));
  }
  else
    return (uint32_t)((GetTotalTimeSec() - (*it).second));
}

Route::TTurns::const_iterator Route::GetCurrentTurn() const
{
  ASSERT(!m_turns.empty(), ());

  turns::TurnItem t;
  t.m_index = static_cast<uint32_t>(m_poly.GetCurrentIter().m_ind);
  return upper_bound(m_turns.cbegin(), m_turns.cend(), t,
         [](turns::TurnItem const & lhs, turns::TurnItem const & rhs)
         {
           return lhs.m_index < rhs.m_index;
         });
}

void Route::GetCurrentStreetName(string & name) const
{
  auto it = GetCurrentStreetNameIterAfter(m_poly.GetCurrentIter());
  if (it == m_streets.cend())
    name.clear();
  else
    name = it->second;
}

void Route::GetStreetNameAfterIdx(uint32_t idx, string & name) const
{
  name.clear();
  auto polyIter = m_poly.GetIterToIndex(idx);
  auto it = GetCurrentStreetNameIterAfter(polyIter);
  if (it == m_streets.cend())
    return;
  for (;it != m_streets.cend(); ++it)
    if (!it->second.empty())
    {
      if (m_poly.GetDistanceM(polyIter, m_poly.GetIterToIndex(max(it->first, static_cast<uint32_t>(polyIter.m_ind)))) < kSteetNameLinkMeters)
        name = it->second;
      return;
    }
}

Route::TStreets::const_iterator Route::GetCurrentStreetNameIterAfter(FollowedPolyline::Iter iter) const
{
  // m_streets empty for pedestrian router.
  if (m_streets.empty())
  {
    return m_streets.cend();
  }

  TStreets::const_iterator curIter = m_streets.cbegin();
  TStreets::const_iterator prevIter = curIter;
  curIter++;

  while (curIter->first < iter.m_ind)
  {
    ++prevIter;
    ++curIter;
    if (curIter == m_streets.cend())
      return curIter;
  }
  return curIter->first == iter.m_ind ? curIter : prevIter;
}

bool Route::GetCurrentTurn(double & distanceToTurnMeters, turns::TurnItem & turn) const
{
  auto it = GetCurrentTurn();
  if (it == m_turns.end())
  {
    ASSERT(false, ());
    return false;
  }

  size_t const segIdx = (*it).m_index;
  turn = (*it);
  distanceToTurnMeters = m_poly.GetDistanceM(m_poly.GetCurrentIter(),
                                             m_poly.GetIterToIndex(segIdx));
  return true;
}

bool Route::GetNextTurn(double & distanceToTurnMeters, turns::TurnItem & turn) const
{
  auto it = GetCurrentTurn();
  auto const turnsEnd = m_turns.end();
  ASSERT(it != turnsEnd, ());

  if (it == turnsEnd || (it + 1) == turnsEnd)
  {
    turn = turns::TurnItem();
    distanceToTurnMeters = 0;
    return false;
  }

  it += 1;
  turn = *it;
  distanceToTurnMeters = m_poly.GetDistanceM(m_poly.GetCurrentIter(),
                                             m_poly.GetIterToIndex(it->m_index));
  return true;
}

bool Route::GetNextTurns(vector<turns::TurnItemDist> & turns) const
{
  turns::TurnItemDist currentTurn;
  if (!GetCurrentTurn(currentTurn.m_distMeters, currentTurn.m_turnItem))
    return false;

  turns.clear();
  turns.emplace_back(move(currentTurn));

  turns::TurnItemDist nextTurn;
  if (GetNextTurn(nextTurn.m_distMeters, nextTurn.m_turnItem))
    turns.emplace_back(move(nextTurn));
  return true;
}

void Route::GetCurrentDirectionPoint(m2::PointD & pt) const
{
  if (m_routingSettings.m_keepPedestrianInfo && m_simplifiedPoly.IsValid())
    m_simplifiedPoly.GetCurrentDirectionPoint(pt, kOnEndToleranceM);
  else
    m_poly.GetCurrentDirectionPoint(pt, kOnEndToleranceM);
}

bool Route::MoveIterator(location::GpsInfo const & info) const
{
  double predictDistance = -1.0;
  if (m_currentTime > 0.0 && info.HasSpeed())
  {
    /// @todo Need to distinguish GPS and WiFi locations.
    /// They may have different time metrics in case of incorrect system time on a device.
    double const deltaT = info.m_timestamp - m_currentTime;
    if (deltaT > 0.0 && deltaT < kLocationTimeThreshold)
      predictDistance = info.m_speed * deltaT;
  }

  m2::RectD const rect = MercatorBounds::MetresToXY(
        info.m_longitude, info.m_latitude,
        max(m_routingSettings.m_matchingThresholdM, info.m_horizontalAccuracy));
  FollowedPolyline::Iter const res = m_poly.UpdateProjectionByPrediction(rect, predictDistance);
  if (m_simplifiedPoly.IsValid())
    m_simplifiedPoly.UpdateProjectionByPrediction(rect, predictDistance);
  return res.IsValid();
}

double Route::GetPolySegAngle(size_t ind) const
{
  size_t const polySz = m_poly.GetPolyline().GetSize();

  if (ind + 1 >= polySz)
  {
    ASSERT(false, ());
    return 0;
  }

  m2::PointD const p1 = m_poly.GetPolyline().GetPoint(ind);
  m2::PointD p2;
  size_t i = ind + 1;
  do
  {
    p2 = m_poly.GetPolyline().GetPoint(i);
  }
  while (m2::AlmostEqualULPs(p1, p2) && ++i < polySz);
  return (i == polySz) ? 0 : my::RadToDeg(ang::AngleTo(p1, p2));
}

void Route::MatchLocationToRoute(location::GpsInfo & location, location::RouteMatchingInfo & routeMatchingInfo) const
{
  if (m_poly.IsValid())
  {
    auto const & iter = m_poly.GetCurrentIter();
    m2::PointD const locationMerc = MercatorBounds::FromLatLon(location.m_latitude, location.m_longitude);
    double const distFromRouteM = MercatorBounds::DistanceOnEarth(iter.m_pt, locationMerc);
    if (distFromRouteM < m_routingSettings.m_matchingThresholdM)
    {
      location.m_latitude = MercatorBounds::YToLat(iter.m_pt.y);
      location.m_longitude = MercatorBounds::XToLon(iter.m_pt.x);
      if (m_routingSettings.m_matchRoute)
        location.m_bearing = location::AngleToBearing(GetPolySegAngle(iter.m_ind));

      routeMatchingInfo.Set(iter.m_pt, iter.m_ind, GetMercatorDistanceFromBegin());
    }
  }
}

bool Route::IsCurrentOnEnd() const
{
  return (m_poly.GetDistanceToEndM() < kOnEndToleranceM);
}

void Route::Update()
{
  if (!m_poly.IsValid())
    return;
  if (m_routingSettings.m_keepPedestrianInfo)
  {
    vector<m2::PointD> points;
    auto distFn = m2::DistanceToLineSquare<m2::PointD>();
    // TODO (ldargunov) Rewrite dist f to distance in meters and avoid 0.00000 constants.
    SimplifyNearOptimal(20, m_poly.GetPolyline().Begin(), m_poly.GetPolyline().End(), 0.00000001, distFn,
                        MakeBackInsertFunctor(points));
    FollowedPolyline(points.begin(), points.end()).Swap(m_simplifiedPoly);
  }
  else
  {
    // Free memory if we don't need simplified geometry.
    FollowedPolyline().Swap(m_simplifiedPoly);
  }
  m_currentTime = 0.0;
}

string DebugPrint(Route const & r)
{
  return DebugPrint(r.m_poly.GetPolyline());
}

} // namespace routing
