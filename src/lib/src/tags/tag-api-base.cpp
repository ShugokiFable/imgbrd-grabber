#include "tags/tag-api-base.h"
#include <QString>
#include "logger.h"
#include "models/api/api.h"
#include "models/site.h"
#include "network/network-follow.h"
#include "network/network-reply.h"


TagApiBase::TagApiBase(Profile *profile, Site *site, Api *api, QObject *parent)
	: QObject(parent), m_profile(profile), m_site(site), m_api(api), m_reply(nullptr)
{}

TagApiBase::~TagApiBase()
{
	if (m_reply != nullptr) {
		m_reply->deleteLater();
	}
}

void TagApiBase::setUrl(QUrl url, QMap<QString, QString> headers)
{
	m_url = std::move(url);
	m_headers = std::move(headers);
}

void TagApiBase::load(bool rateLimit, bool continueChain)
{
	if (!continueChain) {
		m_redirectsSeen.clear();
		m_redirectHops = 0;
		m_rateLimitRetries = 0;
	}

	log(QStringLiteral("[%1] Loading tags page `%2`").arg(m_site->url(), m_url.toString()), Logger::Info);

	if (m_reply != nullptr) {
		if (m_reply->isRunning()) {
			m_reply->abort();
		}

		m_reply->deleteLater();
	}

	Site::QueryType type = rateLimit ? Site::QueryType::Retry : Site::QueryType::List;
	m_reply = m_site->get(m_url, type, {}, "", nullptr, m_headers);
	connect(m_reply, &NetworkReply::finished, this, &TagApiBase::parseInternal);
}

void TagApiBase::abort()
{
	if (m_reply != nullptr && m_reply->isRunning()) {
		m_reply->abort();
	}
}

void TagApiBase::parseInternal()
{
	log(QStringLiteral("[%1] Receiving tags page `%2`").arg(m_site->url(), m_reply->url().toString()), Logger::Info);

	// Check redirection
	QUrl redirection = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
	if (!redirection.isEmpty()) {
		QUrl newUrl = m_site->fixUrl(redirection.toString(), m_url);
		log(QStringLiteral("[%1] Redirecting tags page `%2` to `%3`").arg(m_site->url(), m_url.toString(), newUrl.toString()), Logger::Info);
		QString redirectReason;
		if (NetworkFollow::takeRedirect(m_url, newUrl, &m_redirectsSeen, &m_redirectHops, &redirectReason) == NetworkFollow::Action::Stop) {
			log(QStringLiteral("[%1] Stopping tag redirects: %2").arg(m_site->url(), redirectReason), Logger::Warning);
			emit finishedLoading(this, LoadResult::Error);
			return;
		}
		m_url = newUrl;
		load(false, true);
		return;
	}

	// Detect HTTP 429 usage limit reached
	const int statusCode = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (statusCode == 429) {
		QString retryReason;
		if (NetworkFollow::takeRetry(&m_rateLimitRetries, &retryReason) == NetworkFollow::Action::Stop) {
			log(QStringLiteral("[%1][%2] Giving up after rate limit (%3): %4").arg(m_site->url(), m_api->getName(), QString::number(statusCode), retryReason), Logger::Warning);
			emit finishedLoading(this, LoadResult::Error);
			return;
		}
		log(QStringLiteral("[%1][%2] Limit reached (%3). New try.").arg(m_site->url(), m_api->getName(), QString::number(statusCode)), Logger::Warning);
		load(true, true);
		return;
	}

	// Try to read the reply
	QString source = m_reply->readAll();
	if (source.isEmpty()) {
		if (m_reply->error() != NetworkReply::NetworkError::OperationCanceledError) {
			log(QStringLiteral("[%1][%2] Loading error: %3 (%4)").arg(m_site->url(), m_api->getName(), m_reply->errorString()).arg(m_reply->error()), Logger::Error);
		}
		emit finishedLoading(this, LoadResult::Error);
		return;
	}

	// Parse source
	parse(source, statusCode, m_site);
}
