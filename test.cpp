#pragma clang diagnostic push
#pragma ide diagnostic ignored "cert-err58-cpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <regex>

#include "doctest.h"
#include "flp.h"
using namespace finix;
using namespace std::string_literals;
TEST_CASE("Command without argument: typical usage") {
  LineProtocol flp;
  int call_count = 0;
  call_count = 0;

  flp.RegisterCommand("test", {}, [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
    call_count++;
  });

  // simulate a process call when no input has been fed
  CHECK_FALSE(flp.Process());

  flp.Feed("test\n");
  CHECK(flp.Process());
  CHECK_EQ(call_count, 1);
}

TEST_CASE("Command an integer optional argument") {
  LineProtocol flp;
  int arg;
  int call_count = 0;
  flp.RegisterCommand("test",
                      {{"arg", ArgumentSpec(arg)}},
                      [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                        CHECK(matched.at("arg") == 5.0);
                        CHECK(unmatched.at("other") == 10.0);
                        call_count++;
                      });
  flp.Feed("test arg=5 other=10\n");
  CHECK(flp.Process());
  CHECK_EQ(call_count, 1);

  // should fail if call with float
  flp.Feed("test arg=5.0 other=10.0\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);
}

TEST_CASE("Command an float optional argument") {
  LineProtocol flp;
  float arg;
  int call_count = 0;
  flp.RegisterCommand("test",
                      {{"arg", ArgumentSpec(arg)}},
                      [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                        CHECK(matched.at("arg") == 5.0);
                        CHECK(unmatched.at("other") == 10.0);
                        call_count++;
                      });
  flp.Feed("test arg=5.0 other=10.0\n");
  CHECK(flp.Process());
  CHECK_EQ(call_count, 1);

  // should not fail if call with float
  flp.Feed("test arg=5 other=10\n");
  CHECK(flp.Process());
  CHECK_EQ(call_count, 2);
}

TEST_CASE("Internal commands") {
  std::stringstream ss;
  LineProtocol flp;
  flp.RegisterInternalCommands();
  flp.SetOStream(ss);

  {
    ss.str("");
    flp.Feed("@flp.version\n");
    CHECK(flp.Process());
    std::regex reg(R"(_\((\d+)\) @flp\.version: (\S+)\n)");
    auto s = ss.str();
    std::smatch matches;
    bool found = std::regex_search(s, matches, reg);
    CHECK(found);

    CHECK_EQ(matches[2], FLP_VERSION);
  }

  {
    ss.str("");
    flp.Feed("@flp.buffer.size\n");
    CHECK(flp.Process());
    std::regex reg(R"(_\((\d+)\) @flp\.buffer\.size: 0\n)");
    auto s = ss.str();
    CHECK(std::regex_match(s, reg));
  }

  {
    // No check for this command yet.
    ss.str("");
    flp.Feed("@flp.registration\n");
    CHECK(flp.Process());
    std::cout << ss.str() << "\n";

    int arg;
    float farg;
    flp.RegisterCommand("test",
                        {{"int_opt", ArgumentSpec(arg)},
                         {"int_req", ArgumentSpec(arg, false)},
                         {"f_opt", ArgumentSpec(farg)},
                         {"f_req", ArgumentSpec(farg, false)}},
                        nullptr);
    ss.str("");
    flp.Feed("@flp.registration\n");
    CHECK(flp.Process());
    std::cout << ss.str() << "\n";
  }
}
TEST_CASE("Invalid argument usage") {
  LineProtocol flp;
  float arg;
  flp.RegisterCommand("test",
                      {{"arg", ArgumentSpec(arg)}},
                      nullptr);
  flp.Feed("test arg\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);

  flp.Feed("test arg=\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);

  flp.Feed("test arg=strval\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);
}

TEST_CASE("Enforce required arguments") {
  LineProtocol flp;
  float arg;
  flp.RegisterCommand("test",
                      {{"required_arg", ArgumentSpec(arg, false)},
                       {"optional_arg", ArgumentSpec(arg)}},
                      [](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      });
  flp.Feed("test\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);

  flp.Feed("test optional_arg=1.0\n");
  CHECK_THROWS_AS(flp.Process(), InvalidArgumentError);

  flp.Feed("test required_arg=1.0\n");
  CHECK(flp.Process());

  flp.Feed("test required_arg=1.0 optional_arg=1.0\n");
  CHECK(flp.Process());
}

TEST_CASE("Validation") {
  LineProtocol flp;
  float arg;
  flp.RegisterCommand("test",
                      {{"arg", ArgumentSpec(arg, true, [](float v) { return v > 50.0; })}},
                      [](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
                      });
  flp.Feed("test arg=5\n");
  CHECK_THROWS_AS(flp.Process(), ValidatorError);

  flp.Feed("test arg=500\n");
  CHECK(flp.Process());
}
TEST_CASE("Command without argument: feed by batch") {
  LineProtocol flp;
  int call_count = 0;
  call_count = 0;

  flp.RegisterCommand("test", {}, [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
    call_count++;
  });
  call_count = 0;
  flp.Feed("tes");
  CHECK_FALSE(flp.Process());
  flp.Feed("t\n");
  CHECK(flp.Process());
  CHECK_EQ(call_count, 1);
}

TEST_CASE("Non existing command") {
  LineProtocol flp;
  flp.Feed("test\n");
  CHECK_THROWS_AS(flp.Process(), UnknownQualifierError);

  flp.RegisterCommand("test", {}, [](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {});
  flp.Feed("test\n");
  CHECK(flp.Process());

  flp.Feed("test1\n");
  CHECK_THROWS_AS(flp.Process(), UnknownQualifierError);
}

TEST_CASE("delim and whitespace in-between should be purged") {
  LineProtocol flp;
  flp.Feed("\n\n\n\n\n");
  CHECK_FALSE(flp.Process());
  CHECK(flp.GetBuffer().empty());

  flp.Feed("  \n  \n  \n \n");
  CHECK_FALSE(flp.Process());
  CHECK(flp.GetBuffer().empty());
}

TEST_CASE("Leading or tailing spaces should not affect command") {
  LineProtocol flp;
  int call_count = 0;
  flp.RegisterCommand("test", {}, [&](const RawArgumentMap& matched, const RawArgumentMap& unmatched) {
    call_count++;
  });
  flp.Feed("test   \n");
  flp.Feed("   test\n");
  flp.Feed("   test   \n");
  CHECK(flp.Process());
  CHECK(flp.Process());
  CHECK(flp.Process());
  CHECK_EQ(call_count, 3);
}

#pragma clang diagnostic pop