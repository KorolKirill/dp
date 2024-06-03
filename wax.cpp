#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <math.h>
#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>
#include <eosio/singleton.hpp>
#include "util/atomicassets-interface.hpp"
#include "util/delphioracle-interface.hpp"
#include "util/randomness_provider.cpp"
#include "atomic/atomicdata.hpp"
#include "token/token.hpp"

using namespace std;
using namespace eosio;

CONTRACT wax : public eosio::contract
{
public:
    using contract::contract;

    [[eosio::action]] void init(uint64_t total_profitability,asset total_burned, 
   asset token_creation_s, uint8_t ranks_available, uint8_t withdraw_fee,  uint8_t pack_discount,  double minimal_price)
    {
        require_auth(_self);
        config.get_or_create(get_self(), config_s{});
        auto state = config.get();
        state.total_profitability = total_profitability;
        state.total_burned = total_burned;
        state.token_creation_s = token_creation_s;
        config.set(state, _self);
    }


    [[eosio::action]] void setbuildcfg(
        int32_t template_id,
        uint64_t profitability,
        uint8_t rank,
        double price_var)
    {

        require_auth(_self);
        auto buildings_cgf_itr = buildings_cgf.find(template_id);
        if (buildings_cgf_itr == buildings_cgf.end())
        {
            buildings_cgf.emplace(_self, [&](auto &entry)
                                  {
                                                entry.template_id = template_id;
                                                entry.profitability = profitability;
                                                entry.rank = rank; 
                                                    entry.price_var = price_var;  });
        }
        else
        {
            buildings_cgf.modify(buildings_cgf_itr, get_self(), [&](auto &entry)
                                 { 
                                                entry.profitability = profitability;
                                                entry.rank = rank; 
                                                                entry.price_var = price_var; });
        }
    }

      // clears user balance table.
    // Can be removed.
    [[eosio::action]] void deluserbal(std::vector<name> accounts)
    {
        require_auth(_self);
        for (int i = 0; i < accounts.size(); i++)
        {
            auto accountBalTableitr = users.find(accounts[i].value);
            users.erase(accountBalTableitr);
        }
    }




    // Checked.
    [[eosio::on_notify("atomicassets::transfer")]] void staket(name from, name to, std::vector<uint64_t> asset_ids, std::string memo)
    {
        if ((memo == "stake" || memo == "dep") && to == get_self() && from != get_self())
        {
            // can stake only 1 nft per trx
            can_play(from);
            check(asset_ids.size() > 0, "At least one nft should be transferred.");
            for (uint32_t index = 0; index < asset_ids.size(); index++)
            {
                uint64_t cur_asset = asset_ids[index]; // working with this asset id.

                atomicassets::assets_t owner_assets(atomicassets::ATOMICASSETS_ACCOUNT, get_self().value);
                auto asset_itr = owner_assets.find(cur_asset);
                check(asset_itr->collection_name == collection_name, "Wrong collection");
                auto buildings = get_buildings(from);
                if (asset_itr->schema_name == building_shema)
                {
                    auto building_cfg_itr = buildings_cgf.find(asset_itr->template_id);
                    check(building_cfg_itr != buildings_cgf.end(), "No config was found for your building");
                    // limit for 4 building of the same template id

                    check(get_number_staked_buildings(from, asset_itr->template_id) < 4 , "You can not stake more buildings of this rank.");
                    // acces to config table
                    auto user_itr = users.find(from.value);
                    if (get_number_staked_buildings(from, asset_itr->template_id) > 3)
                    {
                        if (user_itr->max_staked < 4)
                        {
                            users.modify(user_itr, get_self(), [&](auto &entry)
                                         { entry.max_staked = 4; });
                        }
                        else
                        {
                            users.modify(user_itr, get_self(), [&](auto &entry)
                                         { entry.max_staked += 1; });
                        }
                    }

                    users.modify(user_itr, get_self(), [&](auto &entry)
                                 { entry.profitability += building_cfg_itr->profitability; });

                    // acces our tools table
                    buildings.emplace(_self, [&](auto &entry)
                                      {
                    entry.asset_id = cur_asset;
                    entry.template_id = asset_itr->template_id;
                    entry.last_claimed = now(); });
                }
                else
                {
                    check(1 == 2, "error");
                }
            }
        }
    }

    [[eosio::action]] void log(string text)
    {
        require_auth(get_self());
    }


    [[eosio::on_notify("modernworldt::transfer")]] void deposit(eosio::name from, eosio::name to, eosio::asset quantity, std::string memo)
    {
        check(quantity.amount > 0, "Amount is less than 0 ");

        if (memo == "deposits")
        {
            check(to == get_self() && from != get_self(), "Can`t transfer to self");
            can_play(from);
            transfer_token_a.send(get_self(), token_account, quantity, std::string("retire"));
            retire_a.send(quantity, std::string("retire"));
            internal_add_token(from, quantity);
        }
    }

    [[eosio::on_notify("modernworldt::transfers")]] void deposits(name from, name to, std::vector<asset> quantities, std::string memo)
    {
        for (int i = 0; i < quantities.size(); i++)
        {
            check(quantities[i].amount > 0, "Amount " + quantities[i].symbol.code().to_string() + " < 0");
        }

        if (memo == "deposits")
        {
            can_play(from);
            transfers_token_a.send(get_self(), token_account, quantities, std::string("retire"));
            for (int i = 0; i < quantities.size(); i++)
            {
                retire_a.send(quantities[i], std::string("retire"));
            }

            internal_add_tokens(from, quantities);
        }
        // Memo size can be changed. 6 because templated id - 6 digits.
    }

    [[eosio::action]] void withdraw(name owner, std::vector<asset> quantities)
    {
        require_auth(owner);
        can_play(owner);
        for (int i = 0; i < quantities.size(); i++)
        {
            check(quantities[i].symbol == token_empty.symbol, "Wrong params");

            check(quantities[i].amount >= 10, "Amount that is less than 0.001 can not be withdrawn");
            // check(quantities[i].symbol == token_empty.symbol, "Only money can be withdrawn");
        }
        internal_decrease_tokens(owner, quantities);

        auto confTableItr = config.get(); // get confTable to get current fee
        double fee = (double)confTableItr.withdraw_fee;
        vector<asset> dump = quantities;
        for (int i = 0; i < quantities.size(); i++)
        {
            if (quantities[i].symbol == token_empty.symbol)
            {
                dump = {quantities[i]};
            }
        }

        quantities = dump;
        if (!(quantities.size() == 1 && quantities[0].symbol == token_empty.symbol))
        {
            return;
        }

        // issue tokens
        for (int i = 0; i < quantities.size(); i++)
        {
            issue_a.send(get_self(), get_self(), quantities[i], std::string(""));
        }

        std::vector<asset> feeWithdrawRemainder;
        for (int i = 0; i < quantities.size(); i++)
        {
            auto concumedAmount = (uint64_t)((double)quantities[i].amount * fee / 100); // caclulate amount of tockens that we need to charge
            auto remainder = quantities[i].amount - concumedAmount;
            quantities[i].amount = remainder;
            feeWithdrawRemainder.push_back(asset(concumedAmount, quantities[i].symbol));
        }
        // tokens to owner account with deducted fee.
        transfers_token_a.send(get_self(), owner, quantities, std::string("withdraw"));

        for (int i = 0; i < feeWithdrawRemainder.size(); i++)
        {
            if (feeWithdrawRemainder[i].amount > 0)
            {
                burn_token(owner, feeWithdrawRemainder[i], std::string("Burned fee withdrawal"));
            }
        }
    }


    [[eosio::action]] void buybuilding(name buyer, asset cost)
    {
        require_auth(buyer);
        can_play(buyer);
        check(cost.amount > 1, "Amount should be positive");
        check(cost.symbol == token_empty.symbol, "Wrong params");
        auto config_itr = config.get();
        double pack_price = 0;
        uint16_t rank_start = 1;
        for (uint16_t i = 1; i <= config_itr.ranks_available - 6; i++)
        {
            if (pack_price > config_itr.minimal_price)
            {
                break;
            }
            pack_price = calculate_pack_price(i);
            rank_start = i;
        }
        if (!tolerance_check(5, pack_price, (double)cost.amount / pow(10, 4)))
        {
            check(1 == 2, "Price to update was changed, try again...");
        }

        uint64_t ramdomnumber = RandomnessProvider(get_trx_id()).get_rand(100);
        
        bool check = true;
        for (uint8_t i = 0; i < config_itr.pack_chances.size(); i++)
        {
            if (ramdomnumber >= config_itr.pack_chances[i] && check)
            {
                ramdomnumber -= config_itr.pack_chances[i];
                rank_start += 3;
            }
            else
            {
                check = false;
            }
        }
        
        // charege cost
        internal_decrease_token(buyer, cost);
        issue_a.send(get_self(), get_self(), cost, std::string("tokents to burn"));
        burn_token(buyer, cost, std::string("pack bought"));
        // give reward
        auto indexed_by_rank = buildings_cgf.get_index<"rank"_n>();
        auto buildings_cgf_itr = indexed_by_rank.find(rank_start);
        increase_total_power(buildings_cgf_itr->profitability);
        mintNNFT(collection_name, building_shema, buildings_cgf_itr->template_id, buyer);
    }

    checksum256 get_trx_id()
    {
        size_t size = transaction_size();
        char buf[size];
        size_t read = read_transaction(buf, size);
        check(size == read, "read_transaction failed");
        return sha256(buf, read);
    }


    [[eosio::action]] void unstake(name asset_owner, vector<uint64_t> asset_ids)
    {

        require_auth(asset_owner);

        can_play(asset_owner);
        check(asset_ids.size() > 0, "Wrong params");
        // we need to find out what is the schema of the nft. tool or membership?
        std::vector<uint64_t> asset_ids_send;

        for (int i = 0; i < asset_ids.size(); i++)
        {
            auto asset_id = asset_ids[i];
            atomicassets::assets_t owner_assets(atomicassets::ATOMICASSETS_ACCOUNT, get_self().value);
            auto asset_itr = owner_assets.find(asset_id);

            if (asset_itr->schema_name == building_shema)
            {
                auto buildings = get_buildings(asset_owner);
                auto buildings_itr = buildings.find(asset_id);

                check(buildings_itr != buildings.end(), "This building was not staked");
                auto buildings_cgf_itr = buildings_cgf.require_find(buildings_itr->template_id, "Config is not found.");
                claim_internal(asset_owner, asset_id);

                // Can't be unstaked cause the bonus is applied.

                increase_total_power((-1) * floor(  buildings_cgf_itr->profitability));
              
                // delete entry in table;
                buildings.erase(buildings_itr);
                auto user_itr = users.find(asset_owner.value);

                users.modify(user_itr, get_self(), [&](auto &entry)
                             { entry.profitability -= buildings_cgf_itr->profitability; });

                asset_ids_send.push_back(asset_id);
            }
            else
            {
                check(1 == 2, "Something goes wrong");
            }
            // asset_id
        }
        // send nft to user
        action{
            permission_level{get_self(), "active"_n},
            atomicassets::ATOMICASSETS_ACCOUNT,
            "transfer"_n,
            std::make_tuple(get_self(), asset_owner, asset_ids_send, std::string("unstake"))}
            .send();
    }

    [[eosio::action]] void claimall(name owner, std::vector<uint64_t> asset_ids)
    {
        require_auth(owner);
        can_play(owner);
        for (int i = 0; i < asset_ids.size(); i++)
        {
            claim_internal(owner, asset_ids[i]);
        }
    }

    [[eosio::action]] void claim(name asset_owner, uint64_t asset_id)
    {
        require_auth(asset_owner);
        can_play(asset_owner);
        claim_internal(asset_owner, asset_id);
    }

    void claim_internal(name asset_owner, uint64_t asset_id)
    {
        auto buildings = get_buildings(asset_owner);
        auto buildings_itr = buildings.find(asset_id);
        check(buildings_itr != buildings.end(), "The building was not staked");
        check(now() > buildings_itr->last_claimed, "Error.");

        auto buildings_cgf_itr = buildings_cgf.require_find(buildings_itr->template_id, "Problem with a config has occured.");
        double profitability = (double)buildings_cgf_itr->profitability;
        asset reward = calculate_reward(profitability, now() - buildings_itr->last_claimed);
        // log_a.send(to_string(reward.amount));

        buildings.modify(buildings_itr, get_self(), [&](auto &entry)
                         { entry.last_claimed = now(); });

        auto state = config.get();
        if (state.tokens_minted.symbol == reward.symbol)
        {
            state.tokens_minted += reward;
            config.set(state, _self);
        }

        internal_add_token(asset_owner, reward);
    }

    [[eosio::action]] void signup(name user)
    {
        require_auth(user);
        auto users_itr = users.find(user.value);
        check(users_itr == users.end(), "Account has been already registered");
          users.emplace(user, [&](auto &item)
                          {
                            item.account = user;
                            item.profitability = 0;
                            item.balance = tokens_empty;
                            });
    }

    

private:
    symbol WAX_symbol = symbol(symbol_code("WAX"), 8);
    vector<asset> tokens_empty = {
        asset(0, symbol(symbol_code("MWM"), 4))};
    asset token_empty = asset(0, symbol(symbol_code("MWM"), 4));
    name collection_name = "modernworlds"_n;
    name building_shema = "building"_n;
    name token_account = "modernworldt"_n;

    void can_play(name user)
    {
        auto configTableItr = config.get();
        auto users_itr = users.find(user.value);
        check(users_itr != users.end(), "Account is not registered, use signup action firstly.");
        check(configTableItr.playable, "Game is not playbale now.");
    }


    void internal_add_tokens(name owner, vector<asset> quantities)
    {
        for (uint8_t i = 0; i < quantities.size(); i++)
        {
            internal_add_token(owner, quantities[i]);
        }
    }

    bool tolerance_check(uint16_t tolerance, double first_number, double second_number)
    {
        return abs((first_number / second_number - 1) * 100) <= tolerance;
    }

    double calculate_pack_price(uint16_t start_from_rank)
    {
        double price = 0;

        auto indexed_by_rank = buildings_cgf.get_index<"rank"_n>();
        // auto buildings_cgf_ranks = indexByName.lower_bound(start_from_rank);
        auto config_itr = config.get();
        uint8_t indexer = 0;
        for (uint16_t i = start_from_rank; i < start_from_rank + 7; i += 3, indexer++)
        {
            auto buildings_cgf_itr = indexed_by_rank.find(i);
            double day_reward = ((double)calculate_reward(buildings_cgf_itr->profitability, 60 * 60 * 24).amount) / pow(10, 4);
            double constant = 1.15;
            price += buildings_cgf_itr->price_var * day_reward * constant * ((double)(config_itr.pack_chances[indexer]) / 100);
        }
        price = price * (100 - config_itr.pack_discount) / 100;
        return price;
    }

    void internal_add_token(name owner, asset quantity)
    {
        if (quantity.amount == 0)
        {
            return;
        }
        check(quantity.amount > 0, "Can't add negative balances");

        auto users_itr = users.find(owner.value);
        if (users_itr == users.end())
        {
            // No balance table row exists yet
            users.emplace(get_self(), [&](auto &_user)
                          {
            _user.account = owner;
            _user.balance = tokens_empty; });
        }
        users_itr = users.find(owner.value);
        // A balance table row already exists for owner
        auto tokens = users_itr->balance;

        for (uint16_t i = 0; i < tokens.size(); i++)
        {
            if (tokens[i].symbol == quantity.symbol)
            {
                tokens[i].amount += quantity.amount;
            }
        }
        users.modify(users_itr, get_self(), [&](auto &_user)
                     { _user.balance = tokens; });
    }

    void increase_total_power(int64_t profitability)
    {
        auto state = config.get();
        state.total_profitability += profitability;
        config.set(state, _self);
    }

    void internal_decrease_tokens(name owner, vector<asset> quantities)
    {
        for (uint8_t i = 0; i < quantities.size(); i++)
        {
            internal_decrease_token(owner, quantities[i]);
        }
    }

    void internal_decrease_token(
        name owner,
        asset quantity)
    {
        check(quantity.amount > 0, "Negative can be passsed.");
        auto users_itr = users.require_find(owner.value,
                                            "The specified account does not have a balance table row");
        auto tokens = users_itr->balance;
        for (uint16_t i = 0; i < tokens.size(); i++)
        {
            if (tokens[i].symbol == quantity.symbol)
            {
                check(tokens[i].amount >= quantity.amount, "Not enough balance");
                tokens[i].amount -= quantity.amount;
                users.modify(users_itr, same_payer, [&](auto &_user)
                             { _user.balance = tokens; });
            }
        }
    }

    void burn_token(name user, asset quantity, string memo)
    {
        // issue_a.send(get_self(), get_self(), quantity, std::string("tokents to burn"));
        check(quantity.amount > 1, "Amount should be positive");
        transfer_token_a.send(get_self(), token_account, quantity, memo);
        auto state = config.get();
        state.total_burned += quantity;
        config.set(state, _self);
        burn_a.send(quantity, memo);
    }

    void burnNFT(uint64_t asset_id)
    {
        action{
            permission_level{get_self(), "active"_n},
            atomicassets::ATOMICASSETS_ACCOUNT,
            "burnasset"_n,
            std::make_tuple(
                _self, asset_id)}
            .send();
    }

    void mintNNFT(name collection_name, name schema_name, uint32_t template_id, name new_owner)
    {
        action{
            permission_level{get_self(), "active"_n},
            atomicassets::ATOMICASSETS_ACCOUNT,
            "mintasset"_n,
            std::make_tuple(
                get_self(), collection_name, schema_name, template_id, new_owner,
                (atomicdata::ATTRIBUTE_MAP){},
                (atomicdata::ATTRIBUTE_MAP){},
                (std::vector<asset>){})}
            .send();
    }

    uint32_t now()
    {
        return current_time_point().sec_since_epoch();
    }

    asset calculate_reward(double profitability, uint32_t time_passed)
    {
        auto config_itr = config.get();
        asset reward = asset(0, symbol(symbol_code("MWM"), 4));
        reward.amount = (uint64_t)floor(time_passed * (double)config_itr.token_creation_s.amount * ((double)profitability / config_itr.total_profitability));
        return reward;
    }
    uint8_t get_number_staked_buildings(name user, int32_t template_id)
    {
        auto buildings = get_buildings(user);
        auto indexByName = buildings.get_index<"template"_n>();
        auto itr_build_by_template_id = indexByName.lower_bound((uint64_t)template_id);
        uint8_t count = 0;
        for (; itr_build_by_template_id != indexByName.upper_bound((uint64_t)template_id); itr_build_by_template_id++)
        {
            if (itr_build_by_template_id->template_id == template_id)
            {
                count++;
            }
        }
        // -1, cause in cycle we somehow receive a counter with count+1, then needed. +
        return count;
    }


    TABLE buildings_s
    {
        uint64_t asset_id;
        int32_t template_id;
        uint32_t last_claimed;
        uint64_t get_key2() const { return (uint64_t)template_id; };
        uint64_t primary_key() const { return asset_id; };
    };
    // typedef multi_index<name("buildings"), buildings_s> buildings_t;
    typedef eosio::multi_index<"buildings"_n, buildings_s,
                               indexed_by<"template"_n, const_mem_fun<buildings_s, uint64_t, &buildings_s::get_key2>>>
        buildings_t;

    buildings_t get_buildings(name player)
    {
        return buildings_t(get_self(), player.value);
    }

    TABLE buildings_cgf_s
    {
        int32_t template_id;
        uint64_t profitability;
        uint8_t rank;
        float price_var;
        uint64_t get_key2() const { return rank; };
        auto primary_key() const { return template_id; };
    };
    typedef eosio::multi_index<"buildingscfg"_n, buildings_cgf_s,
                               indexed_by<"rank"_n, const_mem_fun<buildings_cgf_s, uint64_t, &buildings_cgf_s::get_key2>>>
        buildings_cgf_t;

    TABLE users_s
    {
        name account;
        std::vector<asset> balance;
        uint64_t profitability;
        uint8_t max_staked;
        uint64_t get_key2() const { return profitability; };
        auto primary_key() const { return account.value; };
    };
    typedef eosio::multi_index<"users"_n, users_s,
                               indexed_by<"profit"_n, const_mem_fun<users_s, uint64_t, &users_s::get_key2>>>
        users_t;

    TABLE config_s
    {
        uint64_t total_profitability = 25000;
        asset total_burned = asset(0, symbol(symbol_code("MWM"), 4));
        asset tokens_minted = asset(0, symbol(symbol_code("MWM"), 4));
        asset token_creation_s = asset(5000, symbol(symbol_code("MWM"), 4));
        uint8_t withdraw_fee = 5;
        uint8_t ranks_available = 15;
        double minimal_price = 400;
        uint8_t pack_discount = 0;
        vector<uint16_t> pack_chances = {90, 8, 2};
        bool playable = true;
        auto primary_key() const { return 1; };
    };

    typedef singleton<name("config"), config_s> config_t;
    // https://github.com/EOSIO/eosio.cdt/issues/280
    typedef multi_index<name("config"), config_s> config_t_for_abi;

    config_t config = config_t(get_self(), get_self().value);
    users_t users = users_t(get_self(), get_self().value);
    buildings_cgf_t buildings_cgf = buildings_cgf_t(get_self(), get_self().value);

    // buildings_t buildings = buildings_t(get_self(), get_self().value);

    using log_action_w = action_wrapper<"log"_n, &wax::log>;
    log_action_w log_a = log_action_w(get_self(), {get_self(), "active"_n});

    using claim_action_w = action_wrapper<"claim"_n, &wax::claim>;
    claim_action_w claim_a = claim_action_w(get_self(), {get_self(), "active"_n});

    token::burn_action burn_a = token::burn_action(token_account, {get_self(), "active"_n});
    token::retire_action retire_a = token::retire_action(token_account, {get_self(), "active"_n});
    token::issue_action issue_a = token::issue_action(token_account, {get_self(), "active"_n});
    token::transfers_action transfers_token_a = token::transfers_action(token_account, {get_self(), "active"_n});
    token::transfer_action transfer_token_a = token::transfer_action(token_account, {get_self(), "active"_n});
};
