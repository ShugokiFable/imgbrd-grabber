#include <QUrl>
#include "catch.h"
#include "network/network-follow.h"


TEST_CASE("NetworkFollow", "[network-follow]")
{
	SECTION("Sankaku OIDC login host is an authentication URL")
	{
		const QUrl login("https://login.sankakucomplex.com/oidc/auth?response_type=code&scope=openid&client_id=sankaku-channel-legacy");
		REQUIRE(NetworkFollow::isAuthenticationUrl(login));
	}

	SECTION("Sankaku OIDC interaction path is an authentication URL")
	{
		const QUrl interaction("https://chan.sankakucomplex.com/oidc/interaction/cd4f8ab1-21d2-48b0-806f-a6868c1b38d7");
		REQUIRE(NetworkFollow::isAuthenticationUrl(interaction));
	}

	SECTION("A normal search URL is not an authentication URL")
	{
		const QUrl search("https://chan.sankakucomplex.com/post/index?page=1&tags=Shirime");
		REQUIRE_FALSE(NetworkFollow::isAuthenticationUrl(search));
	}

	SECTION("Search redirecting to OIDC login is stopped")
	{
		QSet<QString> seen;
		int hops = 0;
		QString reason;
		const QUrl from("https://chan.sankakucomplex.com/post/index?page=1&tags=Shirime");
		const QUrl to("https://login.sankakucomplex.com/oidc/auth?response_type=code&scope=openid");

		REQUIRE(NetworkFollow::takeRedirect(from, to, &seen, &hops, &reason) == NetworkFollow::Action::Stop);
		REQUIRE(reason.contains("login"));
		REQUIRE(hops == 0);
	}

	SECTION("A-B-A redirect loop is stopped")
	{
		QSet<QString> seen;
		int hops = 0;
		QString reason;
		const QUrl a("https://example.com/a");
		const QUrl b("https://example.com/b");

		REQUIRE(NetworkFollow::takeRedirect(a, b, &seen, &hops, &reason) == NetworkFollow::Action::Follow);
		REQUIRE(NetworkFollow::takeRedirect(b, a, &seen, &hops, &reason) == NetworkFollow::Action::Stop);
		REQUIRE(reason.contains("loop"));
	}

	SECTION("Hop cap stops a unique-URL bounce")
	{
		QSet<QString> seen;
		int hops = 0;
		QString reason;
		NetworkFollow::Action last = NetworkFollow::Action::Follow;
		for (int i = 0; i < NetworkFollow::MaxRedirects + 1; ++i) {
			const QUrl from(QStringLiteral("https://example.com/hop/%1").arg(i));
			const QUrl to(QStringLiteral("https://example.com/hop/%1").arg(i + 1));
			last = NetworkFollow::takeRedirect(from, to, &seen, &hops, &reason);
		}
		REQUIRE(last == NetworkFollow::Action::Stop);
		REQUIRE(reason.contains("too many redirects"));
		REQUIRE(hops == NetworkFollow::MaxRedirects + 1);
	}

	SECTION("HTTP to HTTPS same-path redirect is followed")
	{
		QSet<QString> seen;
		int hops = 0;
		QString reason;
		const QUrl from("http://gelbooru.com/index.php?page=post");
		const QUrl to("https://gelbooru.com/index.php?page=post");

		REQUIRE(NetworkFollow::takeRedirect(from, to, &seen, &hops, &reason) == NetworkFollow::Action::Follow);
		REQUIRE(hops == 1);
	}

	SECTION("Rate-limit retries stop after MaxRetries")
	{
		int retries = 0;
		QString reason;
		REQUIRE(NetworkFollow::takeRetry(&retries, &reason) == NetworkFollow::Action::Follow);
		REQUIRE(NetworkFollow::takeRetry(&retries, &reason) == NetworkFollow::Action::Follow);
		REQUIRE(NetworkFollow::takeRetry(&retries, &reason) == NetworkFollow::Action::Stop);
		REQUIRE(retries == NetworkFollow::MaxRetries + 1);
		REQUIRE(reason.contains("rate-limit"));
	}
}
