#include <random>
#include <chrono>
#include <ranges>
#include "HashTable.hpp"

namespace vEB_BTree {
    HashTable::ModdedBasicHashFunction::ModdedBasicHashFunction(size_t numBits): numBits{numBits} {
        std::random_device rd;
        std::seed_seq seed{rd(), rd(), rd(), rd(), rd()};
        std::mt19937 generator{seed};
        std::uniform_int_distribution<KeyType> dist{0ull, (1ull << numBits) - 1};
        for(auto& b: shuffleBits) {
            for(size_t i{0}; i < 256; i++) {
                b[i] = dist(generator);
            }
        }
    }
    
    size_t HashTable::ModdedBasicHashFunction::operator() (KeyType key) const {
        size_t res=0;

        std::array<unsigned char, KeySize> entriesToShuffle = std::bit_cast<std::array<unsigned char, KeySize>, KeyType>(key); //Wack this bit_cast thing is.
        for(size_t i{0}; i < KeySize; i++) {
            res ^= shuffleBits[i][entriesToShuffle[i]];
        }
        
        return res;
    }

    size_t HashTable::ModdedBasicHashFunction::operator() (KeyType key, size_t depth) const {
        size_t res=0;

        std::array<unsigned char, KeySize> entriesToShuffle = std::bit_cast<std::array<unsigned char, KeySize>, KeyType>(key); //Wack this bit_cast thing is.
        for(size_t i{0}; i < depth; i++) {
            res ^= shuffleBits[i][entriesToShuffle[i]];
        }
        
        return res;
    }

    HashTable::ModdedBasicHashFunction::Iterator::Iterator(const ModdedBasicHashFunction& hashFunction): hashFunction{hashFunction} {}

    size_t HashTable::ModdedBasicHashFunction::Iterator::Iterator::operator() (ByteType b) {
        curResult ^= hashFunction.shuffleBits[curDepth++][b];
        return curResult;
    }
    
    size_t HashTable::ModdedBasicHashFunction::Iterator::Iterator::operator() (KeyType key, size_t depthToSkipTo) {
        ULLongByteString keyString{key};
        for(; curDepth < depthToSkipTo; curDepth++) {
            curResult ^= hashFunction.shuffleBits[curDepth][keyString.getByte(curDepth)];
        }
        return curResult;
    }
    
    bool HashTable::testEntry(const HashBucket& entry, KeyType key, size_t depth) const {
        return ULLongByteString::comparePrefixes(key, entry.smallestMember.key, depth) && !entry.childMask.empty();
    }

    std::optional<std::reference_wrapper<HashBucket>> HashTable::loadDesiredEntry(KeyType key, size_t depth) {
        std::array<size_t, 2> hashValues{hashFunctions[0](key, depth), hashFunctions[1](key, depth)};
        std::array<std::reference_wrapper<HashBucket>, 2> possibleEntries{tables[0][depth][hashValues[0]], tables[1][depth][hashValues[1]]};
        return loadDesiredEntry(key, depth, possibleEntries);
    }

    std::optional<std::reference_wrapper<HashBucket>> HashTable::loadDesiredEntry(KeyType key, size_t depth, std::array<std::reference_wrapper<HashBucket>, 2> entries) {
        for(HashBucket& b: entries) {
            if(testEntry(b, key, depth)) {
                return b;
            }
        }
        return {};
    }

    std::optional<std::reference_wrapper<const HashBucket>> HashTable::loadDesiredEntry(KeyType key, size_t depth) const {
        std::array<size_t, 2> hashValues{hashFunctions[0](key, depth), hashFunctions[1](key, depth)};
        std::array<std::reference_wrapper<const HashBucket>, 2> possibleEntries{tables[0][depth][hashValues[0]], tables[1][depth][hashValues[1]]};
        return loadDesiredEntry(key, depth, possibleEntries);
    }

    std::optional<std::reference_wrapper<const HashBucket>> HashTable::loadDesiredEntry(KeyType key, size_t depth, std::array<std::reference_wrapper<const HashBucket>, 2> entries) const {
        for(const HashBucket& b: entries) {
            if(testEntry(b, key, depth)) {
                return b;
            }
        }
        return {};
    }
    
    std::optional<std::reference_wrapper<const HashBucket>> HashTable::successorEntry(const HashBucket& entry, ByteType pos, size_t depth) const {
        // size_t index = pos/64; //maybe move these constants out somewhere
        // uint64_t offset = pos & 63;
        // entry.childMask[index] &= ~(offset - 1);
        // uint64_t desiredPos = -1ull; //should be uint8_t but idk why just for compat or smth made it this
        // for(size_t j{index}; j < entry.childMask.size(); j++) {
        //     if(childMask[index] == 0) continue;
        //     desiredPos = _tzcnt_u64(childMask[index]) + j*KeySizeBits;
        //     break;
        // }
        // if(desiredPos == -1ull) return {};
        // //Now get the two possible entries, compare the prefix and the desiredPos together to see which one matches, and then return.
        // KeyType mask = (1ull << (8*depth)) - 1;
        // KeyType keycmp = key & (~mask);
        // KeyType desiredPrefix = keycmp + (desiredPos << (8*(KeySize-depth));
        FastBitset<256> childMask = entry.childMask;
        childMask.clearSmallBits(pos); //Yeah maybe implement function in that to do these two steps in one step, which also maybe could be a bit more efficient, but whatever
        int successorByte = childMask.findSmallestBit();
        if(successorByte == -1) return {};
        ULLongByteString prefix{entry.smallestMember.key};
        prefix.setByte(depth+1, successorByte);
        return loadDesiredEntry(prefix, depth+1);
    }

    std::vector<std::array<std::reference_wrapper<const HashBucket>, 2>> HashTable::loadAllEntries(KeyType key) const {
        std::vector<std::array<std::reference_wrapper<const HashBucket>, 2>> entries; //Annoying that have to initialize it when changing it later.
        std::array<ModdedBasicHashFunction::Iterator, 2> hashIterators{hashFunctions[0], hashFunctions[1]};
        ULLongByteString keyString{key};
        entries.push_back({tables[0][0][0], tables[1][0][0]});
        size_t dep = 1;
        for(; dep <= entries.size(); dep++) {
            //Figure out something nicer to do with these arrays of size two
            entries.push_back({tables[0][dep][hashIterators[0](keyString.getByte(dep))], tables[1][dep][hashIterators[1](keyString.getByte(dep))]});
            // for(size_t i{0}; i < 2; i++) {
            //     entries[dep][i] = tables[i][dep][hashIterators[i](keyString.getByte(dep))];
            // }
        }
        return entries;
    }
    
    std::optional<ValType> HashTable::pointQuery(KeyType key) const {
        // std::array<HashBucket, 2> entries{tables[0][KeySize][hashFunctions[0](key)], tables[1][KeySize][hashFunctions[1](key)]};
        // for(HashBucket entry: entries) {
        //     //For the final "layer" so to say, we demand that there is exactly one key, so let's just say set only smallestMember
        //     if (testEntry(entry, key, 8)) {
        //         return entry.smallestMember.val;
        //     }
        // }
        std::optional<HashBucket> entry = loadDesiredEntry(key, KeySize);
        if(entry.has_value()) {
            return entry->smallestMember.val;
        }
        return {};
    }
    
    std::optional<KeyValPair> HashTable::successorQuery(KeyType key) const {
        // std::array<std::array<HashBucket, 2>, KeySize+1> entries; //Fix order in hash table too cause here its opposite. Doesn't really matter there but whatever
        // std::array<unsigned char, KeySize> entriesToShuffle = std::bit_cast<std::array<unsigned char, KeySize>, KeyType>(key); //Wack this bit_cast thing is.
        // //And fix order in this for loop.
        // //This should be populated by the hash function?
        // for(size_t i{0}; i < 2; i++) {
        //     ModdedBasicHashFunction& h = hashFunctions[0];
        //     uint64_t res = 0;
        //     entries[0][i] = tables[i][0][0];
        //     for(size_t j{1}; j < KeySize+1; j++) {
        //         res ^= h.shuffleBits[j-1][entriesToShuffle[j-1]];
        //         entries[j][i] = tables[i][j][res];
        //     }
        // }
        
        // //Oh wow this function is kinda wrong. Since you don't know kind of at what stage the successor is (cause maybe up to some of the bits that match only people smaller than you exist), there is no point in actually figuring out where the first difference is.
        // //We just need to preload the successor HashBucket for every single HashTable (that matches us).
        // HashBucket pBucket;
        // for(size_t i{0}; i < KeySize+1; i++) {
            
        //     bool foundFirstDifference = true;
        //     for(size_t j{0}; j < 2; j++) {
        //         HashBucket& entry = entries[i][j];
        //         // if (keycmp == entry.smallestMember.first & mask) { //when i = 0 this is guaranteed to succeed. Honestly should make that more clear/program it a bit better
        //         //     foundFirstDifference = false;
        //         //     pBucket = entry;
        //         // }
        //     }
        //     if(foundFirstDifference) {
        //         //we now go "up" and then "down" a different path
                
        //     }
        // }

        auto entries = loadAllEntries(key);
        std::vector<HashBucket> correctEntries;
        size_t dep = 0;
        for(const auto& entryPair: entries) {
            const auto correctEntry = loadDesiredEntry(key, dep, entryPair);
            if(correctEntry.has_value()) {
                correctEntries.push_back(*correctEntry);
            }
            else {
                break;
            }
            dep++;
        }

        //Keeping the value of dep here is a bit sus.
        ULLongByteString keyString{key};
        for(const auto& entry: correctEntries | std::views::reverse) {
            auto successor = successorEntry(entry, keyString.getByte(dep), dep);
            if(successor.has_value()) {
                return successor->get().smallestMember;
            }
        }

        return {};
    }
};
