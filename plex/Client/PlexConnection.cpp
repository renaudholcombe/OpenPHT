#include "PlexConnection.h"

#include "filesystem/CurlFile.h"

#include <boost/algorithm/string.hpp>

using namespace XFILE;

CPlexConnection::CPlexConnection(int type, const CStdString& host, int port, const CStdString& schema, const CStdString& token) :
  m_type(type), m_state(CONNECTION_STATE_UNKNOWN), m_token(token)
{
  if (host.IsEmpty() || port == 0 || schema.IsEmpty())
  {
    CLog::Log(LOGWARNING, "CPlexConnection::CPlexConnection inited with something that was empty");
  }
  m_url.SetHostName(host);
  m_url.SetPort(port);
  m_url.SetProtocol(schema);

  m_refreshed = true;
  m_http.SetTimeout(3);
  m_http.SetRequestHeader("Accept", "application/xml");
}

CURL
CPlexConnection::BuildURL(const CStdString &path) const
{
  CURL ret(m_url);
  CStdString p(path);

  if (boost::starts_with(path, "/"))
    p = path.substr(1, std::string::npos);

  ret.SetFileName(p);

  if (!GetAccessToken().empty())
    ret.SetOption(GetAccessTokenParameter(), GetAccessToken());

  return ret;
}

CPlexConnection::ConnectionState
CPlexConnection::TestReachability(CPlexServerPtr server)
{
  CURL url = BuildURL("/");
  CStdString rootXml;

  m_http.Reset();

  if (GetAccessToken().empty() && server->HasAuthToken())
    url.SetOption(GetAccessTokenParameter(), server->GetAnyToken());

  if (m_http.Get(url.Get(), rootXml))
  {
    if (server->CollectDataFromRoot(rootXml))
      m_state = CONNECTION_STATE_REACHABLE;
    else
      /* if collect data from root fails, it can be because
       * we got a parser error from root XML == not good.
       * or we got a server we didn't expect == not good.
       * so let's just mark this connection as bad */
      m_state = CONNECTION_STATE_UNREACHABLE;
  }
  else
  {
    if (m_http.DidCancel())
      m_state = CONNECTION_STATE_UNKNOWN;
    else if (m_http.GetLastHTTPResponseCode() == 401)
      m_state = CONNECTION_STATE_UNAUTHORIZED;
    else
      m_state = CONNECTION_STATE_UNREACHABLE;
  }

  return m_state;
}

void
CPlexConnection::Merge(CPlexConnectionPtr otherConnection)
{
  if (!isSSL() || otherConnection->isSSL())
    m_url = otherConnection->m_url;

  m_type |= otherConnection->m_type;

  // If we don't have a token or if the otherConnection have a new token, then we
  // need to use that token instead of our own
  if (m_token.IsEmpty() || (!otherConnection->m_token.IsEmpty() && m_token != otherConnection->m_token))
    m_token = otherConnection->m_token;

  if (m_state != CONNECTION_STATE_REACHABLE && otherConnection->m_state == CONNECTION_STATE_REACHABLE)
    m_state = otherConnection->m_state;

  m_refreshed = true;
}

CStdString CPlexConnection::GetHttpUrl() const
{
  if (m_url.GetProtocol() == "https" && boost::ends_with(m_url.GetHostName(), ".plex.direct"))
  {
    CStdString host = m_url.GetHostName();
    int delimeter = host.Find('.');
    if (delimeter > 0)
    {
      host = host.substr(0, delimeter);
      host.Replace('-', '.');

      CStdString url;
      url.Format("http://%s:%d/", host, m_url.GetPort());
      return url;
    }
  }
  return m_url.Get();
}

bool CPlexConnection::Equals(const CPlexConnectionPtr &other)
{
  if (!other) return false;

  CStdString url1 = GetHttpUrl();
  CStdString url2 = other->GetHttpUrl();

  bool uriMatches = url1.Equals(url2);
  bool tokenMatches;
  if (m_token.IsEmpty() && !other->m_token.IsEmpty())
    tokenMatches = true;
  else if (!m_token.IsEmpty() && other->m_token.IsEmpty())
    tokenMatches = true;
  else
    tokenMatches = m_token.Equals(other->m_token);


  if (!uriMatches)
    CLog::Log(LOGDEBUG, "CPlexConnection::Equals '%s' != '%s'", url1.c_str(), url2.c_str());

  if (!tokenMatches)
    CLog::Log(LOGDEBUG, "CPlexConnection::Equals '%s' != '%s'", m_token.c_str(), other->m_token.c_str());

  return (uriMatches && tokenMatches);
}

CStdString
CPlexConnection::ConnectionStateName(CPlexConnection::ConnectionState state)
{
  switch (state) {
    case CONNECTION_STATE_REACHABLE:
      return "reachable";
      break;
    case CONNECTION_STATE_UNAUTHORIZED:
      return "unauthorized";
      break;
    case CONNECTION_STATE_UNKNOWN:
      return "unknown";
      break;
    default:
      return "unreachable";
      break;
  }
}

CStdString
CPlexConnection::ConnectionTypeName(CPlexConnection::ConnectionType type)
{
  CStdString typeName;
  if (type & CONNECTION_DISCOVERED)
    typeName = "(discovered)";
  if (type & CONNECTION_MANUAL)
    typeName += "(manual)";
  if (type & CONNECTION_MYPLEX)
    typeName += "(plex.tv)";

  return typeName;
}
