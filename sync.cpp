
#include <tinycv.h>
#include <instance.h>
#include <script/script.h>

int tiny::coin_view_version = 2;

std::string rpc_call = "";

std::vector<std::string> fetched_purgable;

void push_purgable(const char* path) {
    fetched_purgable.emplace_back(path);
    while (fetched_purgable.size() > 100) {
        std::string s = fetched_purgable[0];
        fetched_purgable.erase(fetched_purgable.begin());
        unlink(s.c_str());
    }
}

inline FILE* rpc_fetch(const char* cmd, const char* dst, bool abort_on_failure = false) {
    if (rpc_call == "") {
        assert(!"no RPC call available");
    }
    if (system(cmd)) { fprintf(stderr, "failed to run command: %s\n", cmd); exit(1); }
    FILE* fp = fopen(dst, "r");
    if (!fp) {
        fprintf(stderr, "RPC call failed: %s\n", cmd);
        if (!abort_on_failure) {
            fprintf(stderr, "waiting 5 seconds and trying again\n");
            sleep(5);
            return rpc_fetch(cmd, dst, true);
        }
        assert(0);
    }
    push_purgable(dst);
    return fp;
}

template<typename T>
inline void deserialize_hex_string(const char* string, T& object) {
    CDataStream ds(ParseHex(string), SER_DISK, 0);
    ds >> object;
}

template<typename T>
inline void serialize_object(const T& object, std::vector<uint8_t>& bin) {
    CVectorWriter ds(SER_DISK, 0, bin, 0);
    ds << object;
}

void rpc_get_block(const uint256& blockhex, tiny::block& b, uint32_t& height) {
    // printf("get block %s\n", blockhex.ToString().c_str());
    std::string dstfinal = "blockdata/" + blockhex.ToString() + ".mffb";
    FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp) {
        std::string dsthex = "blockdata/" + blockhex.ToString() + ".hex";
        std::string dsthdr = "blockdata/" + blockhex.ToString() + ".hdr";
        FILE* fphex = fopen(dsthex.c_str(), "r");
        FILE* fphdr = fopen(dsthdr.c_str(), "r");
        if (!fphex) {
            std::string cmd = rpc_call + " getblock " + blockhex.ToString() + " 0 > " + dsthex;
            fphex = rpc_fetch(cmd.c_str(), dsthex.c_str());
        }
        if (!fphdr) {
            std::string cmd = rpc_call + " getblockheader " + blockhex.ToString() + " > " + dsthdr;
            fphdr = rpc_fetch(cmd.c_str(), dsthdr.c_str());
        }
        fclose(fphdr);                                      // closes fphdr
        std::string dstheight = std::string("blockdata/") + blockhex.ToString() + ".height";
        std::string cmd = std::string("cat ") + dsthdr + " | jq -r .height > " + dstheight;
        if (system(cmd.c_str())) { fprintf(stderr, "failed to run command: %s\n", cmd.c_str()); exit(1); }
        fphdr = fopen(dstheight.c_str(), "r");
        assert(1 == fscanf(fphdr, "%u", &height));
        fclose(fphdr);                                      // closes fphdr (.height open)
        fseek(fphex, 0, SEEK_END);
        size_t sz = ftell(fphex);
        fseek(fphex, 0, SEEK_SET);
        char* blk = (char*)malloc(sz + 1);
        assert(blk);
        if (sz != fread(blk, 1, sz, fphex)) { fprintf(stderr, "unable to read from input file %s\n", dsthex.c_str()); exit(1); }
        fclose(fphex);                                      // closes fphex
        blk[sz] = 0;
        std::vector<uint8_t> blkdata = ParseHex(blk);
        free(blk);
        fp = fopen(dstfinal.c_str(), "wb+");
        // write height
        fwrite(&height, sizeof(uint32_t), 1, fp);
        // write block
        fwrite(blkdata.data(), 1, blkdata.size(), fp);
        fseek(fp, 0, SEEK_SET);
        // unlink
        unlink(dsthex.c_str());
        unlink(dsthdr.c_str());
        unlink(dstheight.c_str());
    }
    // read height
    if (1 != fread(&height, sizeof(uint32_t), 1, fp)) {
        fprintf(stderr, "unable to read from input file %s\n", dstfinal.c_str());
    }
    // deserialize block
    CAutoFile deserializer(fp, SER_DISK, 0);
    deserializer >> b;
    // deserializer closes fp
}

void rpc_get_block(uint32_t height, tiny::block& b, uint256& blockhex) {
    std::string dstfinal = "blockdata/" + std::to_string(height) + ".hth";
    FILE* fp = fopen(dstfinal.c_str(), "rb");
    if (!fp) {
        std::string dsttxt = "blockdata/" + std::to_string(height) + ".hth.txt";
        FILE* fptxt = fopen(dsttxt.c_str(), "r");
        if (!fptxt) {
            std::string cmd = rpc_call + " getblockhash " + std::to_string(height) + " > " + dsttxt;
            fptxt = rpc_fetch(cmd.c_str(), dsttxt.c_str());
        }
        char hex[128];
        if (1 != fscanf(fptxt, "%s", hex)) { fprintf(stderr, "unable to scan from input file %s\n", dsttxt.c_str()); exit(1); }
        assert(strlen(hex) == 64);
        blockhex = uint256S(hex);
        fclose(fptxt);
        fp = fopen(dstfinal.c_str(), "wb");
        CAutoFile af(fp, SER_DISK, 0);
        af << blockhex;
        push_purgable(dstfinal.c_str());
        return rpc_get_block(blockhex, b, height);
    }
    CAutoFile af(fp, SER_DISK, 0);
    af >> blockhex;
    return rpc_get_block(blockhex, b, height);
}

int print_stack(std::vector<valtype>& stack, bool raw) {
    if (raw) {
        for (auto& it : stack) printf("%s\n", HexStr(it.begin(), it.end()).c_str());
    } else {
        if (stack.size() == 0) printf("- empty stack -\n");
        int i = 0;
        for (int j = stack.size() - 1; j >= 0; j--) {
            auto& it = stack[j];
            i++;
            printf("<%02d>\t%s%s\n", i, HexStr(it.begin(), it.end()).c_str(), i == 1 ? "\t(top)" : "");
        }
    }
    return 0;
}

bool CastToBool(const valtype& vch);

// From chainparams.cpp:
static const auto BIP16Exception = uint256S("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
#define BIP34Height 227931
#define BIP34Hash   uint256S("0x000000000000024b89b42a942fe0d9fea3bb44ab7bd1b19115dd6a759c0808b8")
#define BIP65Height 388381 // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
#define BIP66Height 363725 // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
static const auto MinimalDataExceptTX = std::set<uint256>{
    uint256S("0f24294a1d23efbb49c1765cf443fba7930702752aba6d765870082fe4f13cae"),
    uint256S("7caed7650c392c24e44a093f30438fe6dc9ec1154b92bedc8a561130c9c50920"),
};
#define UpgradableNops 212615 // last offender 212614, tx 03d7e1fa4d5fefa169431f24f7798552861b255cd55d377066fedcd088fb0e99

unsigned int get_flags(int height, const uint256& blockhash, const uint256& txid) {
    unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    if (height < BIP66Height) flags ^= SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S;
    if (height < BIP65Height) flags ^= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    if (height < UpgradableNops) flags ^= SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS;
    if (blockhash == BIP16Exception) flags ^= SCRIPT_VERIFY_P2SH;
    if (MinimalDataExceptTX.count(txid)) flags ^= SCRIPT_VERIFY_MINIMALDATA;
    return flags;
}

int main(int argc, const char** argv)
{
    if (argc == 0) {
        fprintf(stderr, "syntax: %s \"bitcoin RPC call string\"\n", argv[0]);
        return 1;
    }
    rpc_call = argv[1];
    printf("rpc call: %s\n", argv[1]);

    btc_logf = btc_logf_dummy;

    tiny::view view;
    int height = 0;
    uint64_t txs = 0;
    tiny::block b;
    uint256 blockhex;

    // see if we have a state we can read in from
    FILE* fp = fopen("current-sync-state.dat", "rb");
    if (fp) {
        printf("restoring from state..."); fflush(stdout);
        CAutoFile af(fp, SER_DISK, 0);
        af >> height >> view >> txs;
        printf("\n");
    }

    ECCVerifyHandle evh;
    for (;;) {
        ++height;
        txs += b.vtx.size();
        printf("block #%d", height); fflush(stdout);
        rpc_get_block(height, b, blockhex);
        printf("=%s (#tx = %4zu; total = %9llu)\n", blockhex.ToString().c_str(), b.vtx.size(), (unsigned long long)txs);
        
        // process each input of each transaction, except coinbases
        size_t idx = 0;
        for (auto& x : b.vtx) {
            // printf("tx #%zu=%s: ", idx, x.hash.ToString().c_str()); fflush(stdout);
            std::shared_ptr<tiny::tx> ptx = std::make_shared<tiny::tx>(x);
            if (!x.IsCoinBase()) {
                std::string tx_str;
                {
                    std::vector<uint8_t> b;
                    serialize_object(x, b);
                    tx_str = HexStr(b);
                }
                for (int selected = 0; selected < x.vin.size(); ++selected) {
                    auto& vin = x.vin[selected];
                    Instance instance;
                    if (!instance.parse_transaction(tx_str.c_str(), true)) {
                        fprintf(stderr, "block %s, index %zu failed to parse tx %s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str());
                        exit(1);
                    }
                    auto txin = view.get(vin.prevout.hash);
                    if (!txin) {
                        fprintf(stderr, "block %s, index %zu tx %s could not find input tx %s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), vin.prevout.hash.ToString().c_str());
                        exit(1);
                    }
                    {
                        std::vector<uint8_t> b;
                        serialize_object(*txin, b);
                        if (!instance.parse_input_transaction(HexStr(b).c_str(), selected)) {
                            fprintf(stderr, "block %s, index %zu tx %s failed to parse input tx %d=%s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str());
                            exit(1);
                        }
                    }

                    if (!instance.configure_tx_txin()) {
                        fprintf(stderr, "block %s, index %zu tx %s failed to configure tx/txin for input %d=%s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str());
                        exit(1);
                    }
                    bool require_cleanstack = instance.sigver == SigVersion::WITNESS_V0;

                    if (!instance.setup_environment(get_flags(height, blockhex, x.hash))) {
                        fprintf(stderr, "block %s, index %zu tx %s failed to initialize script environment for input %d=%s: %s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str(), instance.error_string());
                        exit(1);
                    }

                    auto& env = instance.env;

                    if (!ContinueScript(*env)) {
                        fprintf(stderr, "block %s, index %zu tx %s failed to validate input %d=%s: %s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str(), instance.error_string());
                        fprintf(stderr, "error: %s\n", ScriptErrorString(*env->serror));
                        return 1;
                    }

                    // stack should have 1 item. it should be true
                    if (env->stack.size() != 1) {
                        fprintf(stderr, "block %s, index %zu tx %s finished execution with non-1 stack size for input %d=%s: size() == %zu\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str(), env->stack.size());
                        if (require_cleanstack) return 1;
                    }
                    if (!CastToBool(env->stack.back())) {
                        fprintf(stderr, "block %s, index %zu tx %s finished execution with non-truthy on stack for input %d=%s: stack top = %s\n", blockhex.ToString().c_str(), idx, x.hash.ToString().c_str(), selected, vin.prevout.hash.ToString().c_str(), HexStr(env->stack[0]).c_str());
                        print_stack(env->stack, false);
                        return 1;
                    }

                    // printf("."); fflush(stdout);
                }
            }
            view.insert(ptx);
            idx++;
            // printf("\n");
        }

        if ((height % 100) == 0) {
            // save view and height to disk
            printf("writing state to disk..."); fflush(stdout);
            {
                FILE* fp = fopen("current-sync-state.new", "wb");
                CAutoFile af(fp, SER_DISK, 0);
                af << height << view << txs;
            }
            {
                tiny::view view2;
                FILE* fp = fopen("current-sync-state.new", "rb");
                if (fp) {
                    CAutoFile af(fp, SER_DISK, 0);
                    af >> height >> view2 >> txs;
                }
                assert(view == view2);
                // FILE* fp2 = fopen("tmpfile", "wb");
                // if (fp2) {
                //     CAutoFile af(fp2, SER_DISK, 0);
                //     af << height << view2 << txs;
                // }
            }
            // {
            //     FILE* fp = fopen("current-sync-state.new", "rb");
            //     FILE* fp2 = fopen("tmpfile", "rb");
            //     char* buf = (char*)malloc(65536);
            //     char* buf2 = (char*)malloc(65536);
            //     size_t z, z2;
            //     while (0 != (z = fread(buf, 1, 65536, fp))) {
            //         z2 = fread(buf2, 1, 65536, fp2);
            //         if (z != z2) {
            //             printf("file sizes differ: read %zu vs %zu b\n", z, z2);
            //             exit(99);
            //         }
            //         if (memcmp(buf, buf2, z)) {
            //             printf("files differ around byte %ld\n", ftell(fp) - z);
            //             exit(99);
            //         }
            //     }
            //     fclose(fp);
            //     fclose(fp2);
            //     free(buf);
            //     free(buf2);
            // }
            unlink("current-sync-state.dat");
            rename("current-sync-state.new", "current-sync-state.dat");
            printf("\n");
            // if ((height % 1000) == 0) {
            //     printf("backing up 1k block state..."); fflush(stdout);
            //     FILE* fp = fopen("current-sync-state.dat", "rb");
            //     FILE* fp2 = fopen("backup-state-1k.dat", "wb");
            //     char* buf = (char*)malloc(65536);
            //     size_t sz;
            //     while (0 < (sz = fread(buf, 1, 65536, fp))) {
            //         (void)fwrite(buf, 1, sz, fp2);
            //     }
            //     fclose(fp);
            //     fclose(fp2);
            //     {
            //         tiny::view view2;
            //         FILE* fp = fopen("backup-state-1k.dat", "rb");
            //         if (fp) {
            //             CAutoFile af(fp, SER_DISK, 0);
            //             af >> height >> view2 >> txs;
            //         }
            //         assert(view == view2);
            //     }
            //     printf("\n");
            // }
        }
    }
}
