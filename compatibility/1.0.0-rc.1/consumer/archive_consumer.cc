// Immutable archive-v1 fixture consumer: loads every fixture listed in
// this snapshot's manifest.json from the snapshot directory passed as
// argv[1]. The fixture list below mirrors the manifest by construction
// and is frozen with it. Nonzero on any failed load or content check.
#include <tess/tess.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
#define CHECK(cond, msg)                                \
  do {                                                  \
    if (!(cond)) {                                      \
      ++failures;                                       \
      std::printf("FAIL line %d: %s\n", __LINE__, msg); \
    }                                                   \
  } while (0)

struct PassableTag {};
struct CostTag {};
using Schema = tess::FieldSchema<tess::Field<PassableTag, bool>,
                                 tess::Field<CostTag, std::uint8_t>>;
using Shape = tess::Shape<tess::Extent3{32, 32, 1}, tess::Extent3{16, 16, 1}>;
using DenseWorld = tess::AlwaysResidentWorld<Shape, Schema>;
using SparseWorld = tess::SparseResidentWorld<Shape, Schema>;
// Fixed schema identities shared with the recorded fixture generator.
using DenseArchive =
    tess::PersistenceSchema<0x64656E73652D3176ULL, 1,
                            tess::PersistedField<PassableTag, 0x70617373ULL, 1>,
                            tess::PersistedField<CostTag, 0x636f7374ULL, 1>>;
using SparseArchive = DenseArchive;

std::vector<std::byte> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::vector<char> chars((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(chars[i]);
  }
  return bytes;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: archive_consumer <snapshot-dir>\n");
    return 2;
  }
  const std::string root = argv[1];

  {
    const auto bytes =
        read_file(root + "/archives/dense-two-field.tessarchive");
    CHECK(!bytes.empty(), "dense fixture reads");
    DenseWorld world;
    CHECK(tess::load_world_archive<DenseArchive>(world, bytes).status ==
              tess::WorldArchiveStatus::Ok,
          "dense fixture loads");
    CHECK(world.field<PassableTag>(tess::Coord3{16, 10, 0}) == false,
          "dense fixture content");
    CHECK(world.field<CostTag>(tess::Coord3{3, 3, 0}) == 9,
          "dense fixture cost content");
  }
  {
    const auto bytes =
        read_file(root + "/archives/sparse-two-field.tessarchive");
    CHECK(!bytes.empty(), "sparse fixture reads");
    SparseWorld world{tess::ResidencyConfig{4 * SparseWorld::page_byte_size}};
    CHECK(tess::load_world_archive<SparseArchive>(world, bytes).status ==
              tess::WorldArchiveStatus::Ok,
          "sparse fixture loads");
    CHECK(world.field<CostTag>(tess::Coord3{3, 3, 0}) == 9,
          "sparse fixture content");
  }

  std::printf("archive consumer: %s (%d)\n",
              failures == 0 ? "ALL FIXTURES LOADED" : "FAILURES", failures);
  return failures == 0 ? 0 : 1;
}
