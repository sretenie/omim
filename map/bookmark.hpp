#pragma once

#include "map/user_mark.hpp"
#include "map/user_mark_container.hpp"
#include "map/styled_point.hpp"

#include "coding/reader.hpp"

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"

#include "base/timer.hpp"

#include "std/string.hpp"
#include "std/noncopyable.hpp"
#include "std/iostream.hpp"
#include "std/shared_ptr.hpp"

namespace anim
{
  class Task;
}

class Track;

class BookmarkData
{
public:
  BookmarkData()
    : m_scale(-1.0)
    , m_timeStamp(my::INVALID_TIME_STAMP)
  {
  }

  BookmarkData(string const & name, string const & type,
                     string const & description = "", double scale = -1.0,
                     time_t timeStamp = my::INVALID_TIME_STAMP,
                     string const & text = "",
                     bool isGroup = false,
                     m2::RectD bounds = m2::RectD(),
                     const int & uid = -1)
    : m_name(name)
    , m_description(description)
    , m_type(type)
    , m_scale(scale)
    , m_timeStamp(timeStamp)
    , m_text(text)
    , m_isGroup(isGroup)
    , m_bounds(bounds)
    , m_uid(uid)
  {
  }

  string const & GetName() const { return m_name; }
  void SetName(const string & name) { m_name = name; }

  string const & GetDescription() const { return m_description; }
  void SetDescription(const string & description) { m_description = description; }

  string const & GetType() const { return m_type; }
  void SetType(const string & type) { m_type = type; }

  double const & GetScale() const { return m_scale; }
  void SetScale(double scale) { m_scale = scale; }

  time_t const & GetTimeStamp() const { return m_timeStamp; }
  void SetTimeStamp(const time_t & timeStamp) { m_timeStamp = timeStamp; }

  string const & GetText() const { return m_text; }
  void SetText(string const & text) { m_text = text; }

  bool const & IsGroup() const { return m_isGroup; }
  void SetIsGroup(bool const & group) { m_isGroup = group; }

  m2::RectD const & GetGroupBounds() const { return m_bounds; }
  void SetGroupBounds(m2::RectD const & bounds) { m_bounds = bounds; }

  int const & GetUid() const { return m_uid; }
  void SetUid(int const & uid) { m_uid = uid; }

private:
  string m_name;
  string m_description;
  string m_type;  ///< Now it stores bookmark color (category style).
  double m_scale; ///< Viewport scale. -1.0 - is a default value (no scale set).
  time_t m_timeStamp;
  string m_text;
  bool m_isGroup;
  m2::RectD m_bounds;
  int m_uid;
};

class Bookmark : public UserMark
{
  using TBase = UserMark;
public:
  Bookmark(m2::PointD const & ptOrg, UserMarkContainer * container);

  Bookmark(m2::PointD const & ptOrg, UserMarkContainer * container,
           bool runCreationAnim);

  Bookmark(BookmarkData const & data, m2::PointD const & ptOrg,
           UserMarkContainer * container);

  Bookmark(BookmarkData const & data, m2::PointD const & ptOrg,
           UserMarkContainer * container, bool runCreationAnim);

  void SetData(BookmarkData const & data);
  BookmarkData const & GetData() const;

  dp::Anchor GetAnchor() const override;
  string GetSymbolName() const override;

  Type GetMarkType() const override;
  void FillLogEvent(TEventContainer & details) const override;
  bool RunCreationAnim() const override;

  string const & GetText() const override { return m_data.GetText(); }
  bool const & IsGroup() const { return m_data.IsGroup(); }
  m2::RectD const & GetGroupBounds() const { return m_data.GetGroupBounds(); }
  int const & GetUid() const { return m_data.GetUid(); }

  string const & GetName() const;
  void SetName(string const & name);
  /// @return Now its a bookmark color - name of icon file
  string const & GetType() const;
  void SetType(string const & type);
  m2::RectD GetViewport() const;

  string const & GetDescription() const;
  void SetDescription(string const & description);

  /// @return my::INVALID_TIME_STAMP if bookmark has no timestamp
  time_t GetTimeStamp() const;
  void SetTimeStamp(time_t timeStamp);

  double GetScale() const;
  void SetScale(double scale);

  unique_ptr<UserMarkCopy> Copy() const override;

private:
  BookmarkData m_data;
  mutable bool m_runCreationAnim;
};

class BookmarkCategory : public UserMarkContainer
{
  typedef UserMarkContainer TBase;
  vector<unique_ptr<Track>> m_tracks;
  deque<unique_ptr<Bookmark>> m_Bookmarks;

  string m_name;
  /// Stores file name from which category was loaded
  string m_file;

public:
  class Guard
  {
  public:
    Guard(BookmarkCategory & cat)
      : m_controller(cat.RequestController())
      , m_cat(cat)
    {
    }

    ~Guard()
    {
      m_cat.ReleaseController();
    }

    UserMarksController & m_controller;

  private:
    BookmarkCategory & m_cat;
  };

  BookmarkCategory(string const & name, Framework & framework);
  ~BookmarkCategory();

  size_t GetUserLineCount() const override;
  df::UserLineMark const * GetUserLineMark(size_t index) const override;

  static string GetDefaultType();

  void ClearTracks();

  /// @name Tracks routine.
  //@{
  void AddTrack(unique_ptr<Track> && track);
  Track const * GetTrack(size_t index) const;
  inline size_t GetTracksCount() const { return m_tracks.size(); }
  void DeleteTrack(size_t index);
  //@}

  void SetName(string const & name) { m_name = name; }
  string const & GetName() const { return m_name; }
  string const & GetFileName() const { return m_file; }

  /// @name Theese fuctions are public for unit tests only.
  /// You don't need to call them from client code.
  //@{
  bool LoadFromKML(ReaderPtr<Reader> const & reader);
  void SaveToKML(ostream & s);

  /// Uses the same file name from which was loaded, or
  /// creates unique file name on first save and uses it every time.
  bool SaveToKMLFile();

  void ClusterMarks(long pixelDistance, unsigned int clusterSize = 0, int minZoom = 6, int maxZoom = 16);

  Bookmark const * GetBookmark(size_t index) const;
  pair<int, Bookmark const *> GetBookmarkByUid(int const & uid) const;

  void DeleteBookmarkByUid(int const & uid);

  void DeleteAllBookmarks();

  /// @return 0 in the case of error
  static BookmarkCategory * CreateFromKMLFile(string const & file, Framework & framework);

  /// Get valid file name from input (remove illegal symbols).
  static string RemoveInvalidSymbols(string const & name);
  /// Get unique bookmark file name from path and valid file name.
  static string GenerateUniqueFileName(const string & path, string name);
  //@}

protected:
  UserMark * AllocateUserMark(m2::PointD const & ptOrg) override;
  UserMark * CreateUserMark(m2::PointD const & ptOrg) override;
  void DeleteUserMark(size_t index) override;
};

/// <category index, bookmark index>
typedef pair<int, int> BookmarkAndCategory;
inline BookmarkAndCategory MakeEmptyBookmarkAndCategory()
{
  return BookmarkAndCategory(int(-1), int(-1));
}

inline bool IsValid(BookmarkAndCategory const & bmc)
{
  return (bmc.first >= 0 && bmc.second >= 0);
}
