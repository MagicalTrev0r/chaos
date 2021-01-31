// Copyright (c) 2011-2017 The Cryptonote developers
// Copyright (c) 2017-2018 The Circle Foundation & Conceal Devs
// Copyright (c) 2018-2019 Conceal Network & Conceal Devs
// Copyright (c) 2020 - The Cache Developers
//
// Distributed under the GNU Lesser General Public License v3.0.
// Please read Cache/License.md

#include "Currency.h"
#include <cctype>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>
#include "../Common/Base58.h"
#include "../Common/int-util.h"
#include "../Common/StringTools.h"
#include "Common/Types.h"

#include "CryptoNoteConfig.h"
#include "Logging/FileLog.h"
#include "Account.h"
#include "CryptoNoteBasicImpl.h"
#include "CryptoNoteFormatUtils.h"
#include "CryptoNoteTools.h"
#include "TransactionExtra.h"
#include "UpgradeDetector.h"

#undef ERROR

using namespace Logging;
using namespace Common;

namespace CryptoNote
{

  const std::vector<Amount> Currency::PRETTY_AMOUNTS = {
      1, 2, 3, 4, 5, 6, 7, 8, 9,
      10, 20, 30, 40, 50, 60, 70, 80, 90,
      100, 200, 300, 400, 500, 600, 700, 800, 900,
      1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000,
      10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000,
      100000, 200000, 300000, 400000, 500000, 600000, 700000, 800000, 900000,
      1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000,
      10000000, 20000000, 30000000, 40000000, 50000000, 60000000, 70000000, 80000000, 90000000,
      100000000, 200000000, 300000000, 400000000, 500000000, 600000000, 700000000, 800000000, 900000000,
      1000000000, 2000000000, 3000000000, 4000000000, 5000000000, 6000000000, 7000000000, 8000000000, 9000000000,
      10000000000, 20000000000, 30000000000, 40000000000, 50000000000, 60000000000, 70000000000, 80000000000, 90000000000,
      100000000000, 200000000000, 300000000000, 400000000000, 500000000000, 600000000000, 700000000000, 800000000000, 900000000000,
      1000000000000, 2000000000000, 3000000000000, 4000000000000, 5000000000000, 6000000000000, 7000000000000, 8000000000000, 9000000000000,
      10000000000000, 20000000000000, 30000000000000, 40000000000000, 50000000000000, 60000000000000, 70000000000000, 80000000000000, 90000000000000,
      100000000000000, 200000000000000, 300000000000000, 400000000000000, 500000000000000, 600000000000000, 700000000000000, 800000000000000, 900000000000000,
      1000000000000000, 2000000000000000, 3000000000000000, 4000000000000000, 5000000000000000, 6000000000000000, 7000000000000000, 8000000000000000, 9000000000000000,
      10000000000000000, 20000000000000000, 30000000000000000, 40000000000000000, 50000000000000000, 60000000000000000, 70000000000000000, 80000000000000000, 90000000000000000,
      100000000000000000, 200000000000000000, 300000000000000000, 400000000000000000, 500000000000000000, 600000000000000000, 700000000000000000, 800000000000000000, 900000000000000000,
      1000000000000000000, 2000000000000000000, 3000000000000000000, 4000000000000000000, 5000000000000000000, 6000000000000000000, 7000000000000000000, 8000000000000000000, 9000000000000000000,
      10000000000000000000ull};

  const std::vector<Amount> Currency::REWARD_INCREASING_FACTOR = { 0, 25000, 50000, 75000, 100000, 125000, 150000, 175000, 200000 };

  bool Currency::init()
  {
    if (!generateGenesisBlock())
    {
      logger(ERROR, BRIGHT_RED) << "Failed to generate genesis block";
      return false;
    }

    if (!get_block_hash(m_genesisBlock, m_genesisBlockHash))
    {
      logger(ERROR, BRIGHT_RED) << "Failed to get genesis block hash";
      return false;
    }

    if (isTestnet())
    {
      m_upgradeHeightV2 = 0;
      m_upgradeHeightV3 = static_cast<Height>(-1);
      m_blocksFileName = "testnet_" + m_blocksFileName;
      m_blocksCacheFileName = "testnet_" + m_blocksCacheFileName;
      m_blockIndexesFileName = "testnet_" + m_blockIndexesFileName;
      m_txPoolFileName = "testnet_" + m_txPoolFileName;
      m_blockchinIndicesFileName = "testnet_" + m_blockchinIndicesFileName;
    }

    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::generateGenesisBlock()
  {
    m_genesisBlock = boost::value_initialized<Block>();

    // Hard code coinbase tx in genesis block, because "tru" generating tx use random, but genesis should be always the same
    std::string genesisCoinbaseTxHex = GENESIS_COINBASE_TX_HEX;
    BinaryArray minerTxBlob;

    bool r =
        fromHex(genesisCoinbaseTxHex, minerTxBlob) &&
        fromBinaryArray(m_genesisBlock.baseTransaction, minerTxBlob);

    if (!r)
    {
      logger(ERROR, BRIGHT_RED) << "failed to parse coinbase tx from hard coded blob";
      return false;
    }

    m_genesisBlock.majorVersion = BLOCK_MAJOR_VERSION_1;
    m_genesisBlock.minorVersion = BLOCK_MINOR_VERSION_0;
    m_genesisBlock.timestamp = GENESIS_TIMESTAMP;
    m_genesisBlock.nonce = GENESIS_NONCE;

    if (m_testnet)
    {
      ++m_genesisBlock.nonce;
    }

    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::baseRewardFunction(uint64_t alreadyGeneratedCoins, uint32_t height) const
  {
    if (height == 1) { return FOUNDATION_TRUST; }
    
    uint64_t incrIntervals = static_cast<uint64_t>(height) / REWARD_INCREASE_INTERVAL;
    //assert(incrIntervals < REWARD_INCREASING_FACTOR.size());
    uint64_t base_reward = BLOCK_REWARD + REWARD_INCREASING_FACTOR[incrIntervals];

    base_reward = (std::min)(base_reward, STATIC_BLOCK_REWARD);
    base_reward = (std::min)(base_reward, m_moneySupply - alreadyGeneratedCoins);

    return base_reward;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint32_t Currency::upgradeHeight(uint8_t majorVersion) const
  {
    if (majorVersion == BLOCK_MAJOR_VERSION_2)
    {
      return m_upgradeHeightV2;
    } else {
      return static_cast<uint32_t>(-1);
    }
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::getBlockReward(size_t medianSize, size_t currentBlockSize, uint64_t alreadyGeneratedCoins,
                                uint64_t fee, uint32_t height, uint64_t &reward, int64_t &emissionChange) const
  {

    assert(alreadyGeneratedCoins <= m_moneySupply);
    uint64_t baseReward = baseRewardFunction(alreadyGeneratedCoins, height);

    medianSize = std::max(medianSize, m_blockGrantedFullRewardZone);
    if (currentBlockSize > UINT64_C(2) * medianSize)
    {
      logger(TRACE) << "Block cumulative size is too big: " << currentBlockSize << ", expected less than " << 2 * medianSize;
      return false;
    }

    uint64_t penalizedBaseReward = getPenalizedAmount(baseReward, medianSize, currentBlockSize);
    uint64_t penalizedFee = getPenalizedAmount(fee, medianSize, currentBlockSize);

    emissionChange = penalizedBaseReward - (fee - penalizedFee);
    reward = penalizedBaseReward + penalizedFee;

    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::calculateInterest(uint64_t amount, uint32_t term) const
  {
    uint64_t returnVal = 0;
    uint64_t amount4Humans = amount / 100000;
    float baseInterest = static_cast<float>(0.049);

    if (amount4Humans >= 5000 && amount4Humans < 10000) { baseInterest = static_cast<float>(0.054); }
    if (amount4Humans >= 10000 && amount4Humans < 15000) { baseInterest = static_cast<float>(0.059); }
    if (amount4Humans >= 15000 && amount4Humans < 20000) { baseInterest = static_cast<float>(0.064); }
    if (amount4Humans >= 20000) { baseInterest = static_cast<float>(0.069); }

    float months = term / 21900;
    if (months > 12) { months = 12; }

    float ear = baseInterest + (months - 1) * 0.001;
    float eir = (ear / 12) * months;
    returnVal = static_cast<uint64_t>(eir);

    float interest = amount * eir;
    returnVal = static_cast<uint64_t>(interest);
    return returnVal;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::calculateTotalTransactionInterest(const Transaction &tx) const
  {
    uint64_t interest = 0;
    for (const TransactionInput &input : tx.inputs)
    {
      if (input.type() == typeid(MultisignatureInput))
      {
        const MultisignatureInput &multisignatureInput = boost::get<MultisignatureInput>(input);
        if (multisignatureInput.term != 0)
        {
          interest += calculateInterest(multisignatureInput.amount, multisignatureInput.term);
        }
      }
    }

    return interest;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::getTransactionInputAmount(const TransactionInput &in, uint32_t height) const
  {
    if (in.type() == typeid(KeyInput))
    {
      return boost::get<KeyInput>(in).amount;
    }
    else if (in.type() == typeid(MultisignatureInput))
    {
      const MultisignatureInput &multisignatureInput = boost::get<MultisignatureInput>(in);
      if (multisignatureInput.term == 0)
      {
        return multisignatureInput.amount;
      }
      else
      {
        return multisignatureInput.amount + calculateInterest(multisignatureInput.amount, multisignatureInput.term);
      }
    }
    else if (in.type() == typeid(BaseInput))
    {
      return 0;
    }
    else
    {
      assert(false);
      return 0;
    }
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::getTransactionAllInputsAmount(const Transaction &tx, uint32_t height) const
  {
    uint64_t amount = 0;
    for (const auto &in : tx.inputs)
    {
      amount += getTransactionInputAmount(in, height);
    }

    return amount;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::getTransactionFee(const Transaction &tx, uint64_t &fee, uint32_t height) const
  {
    uint64_t amount_in = 0;
    uint64_t amount_out = 0;

    //if (tx.inputs.size() == 0)// || tx.outputs.size() == 0) //0 outputs needed in TestGenerator::constructBlock
    //	  return false;

    for (const auto &in : tx.inputs)
    {
      amount_in += getTransactionInputAmount(in, height);
    }

    for (const auto &o : tx.outputs)
    {
      amount_out += o.amount;
    }

    if (amount_out > amount_in)
    {
      // interest shows up in the output of the W/D transactions and W/Ds always have min fee
      if (tx.inputs.size() > 0 && tx.outputs.size() > 0 && amount_out > amount_in + parameters::MINIMUM_FEE)
      {
        fee = parameters::MINIMUM_FEE;
        logger(INFO) << "TRIGGERED: Currency.cpp getTransactionFee";
      }
      else
      {
        return false;
      }
    }
    else
    {
      fee = amount_in - amount_out;
    }

    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  uint64_t Currency::getTransactionFee(const Transaction &tx, uint32_t height) const
  {
    uint64_t r = 0;
    if (!getTransactionFee(tx, r, height))
    {
      r = 0;
    }

    return r;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  size_t Currency::maxBlockCumulativeSize(uint64_t height) const
  {
    assert(height <= std::numeric_limits<uint64_t>::max() / m_maxBlockSizeGrowthSpeedNumerator);
    size_t maxSize = static_cast<size_t>(m_maxBlockSizeInitial +
                                         (height * m_maxBlockSizeGrowthSpeedNumerator) / m_maxBlockSizeGrowthSpeedDenominator);

    assert(maxSize >= m_maxBlockSizeInitial);
    return maxSize;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::constructMinerTx(uint32_t height, size_t medianSize, uint64_t alreadyGeneratedCoins, size_t currentBlockSize,
                                  uint64_t fee, const AccountPublicAddress &minerAddress, Transaction &tx,
                                  const BinaryArray &extraNonce /* = BinaryArray()*/, size_t maxOuts /* = 1*/) const
  {
    tx.inputs.clear();
    tx.outputs.clear();
    tx.extra.clear();

    KeyPair txkey = generateKeyPair();
    addTransactionPublicKeyToExtra(tx.extra, txkey.publicKey);
    if (!extraNonce.empty())
    {
      if (!addExtraNonceToTransactionExtra(tx.extra, extraNonce))
      {
        return false;
      }
    }

    BaseInput in;
    in.blockIndex = height;

    uint64_t blockReward;
    int64_t emissionChange;
    if (!getBlockReward(medianSize, currentBlockSize, alreadyGeneratedCoins, fee, height, blockReward, emissionChange))
    {
      logger(INFO) << "Block is too big";
      return false;
    }

    std::vector<uint64_t> outAmounts;
    decompose_amount_into_digits(
        blockReward, m_defaultDustThreshold,
        [&outAmounts](uint64_t a_chunk) { outAmounts.push_back(a_chunk); },
        [&outAmounts](uint64_t a_dust) { outAmounts.push_back(a_dust); });

    if (!(1 <= maxOuts))
    {
      logger(ERROR, BRIGHT_RED) << "max_out must be non-zero";
      return false;
    }

    while (maxOuts < outAmounts.size())
    {
      outAmounts[outAmounts.size() - 2] += outAmounts.back();
      outAmounts.resize(outAmounts.size() - 1);
    }

    uint64_t summaryAmounts = 0;
    for (size_t no = 0; no < outAmounts.size(); no++)
    {
      Crypto::KeyDerivation derivation = boost::value_initialized<Crypto::KeyDerivation>();
      Crypto::PublicKey outEphemeralPubKey = boost::value_initialized<Crypto::PublicKey>();

      bool r = Crypto::generate_key_derivation(minerAddress.viewPublicKey, txkey.secretKey, derivation);

      if (!(r))
      {
        logger(ERROR, BRIGHT_RED)
            << "while creating outs: failed to generate_key_derivation("
            << minerAddress.viewPublicKey << ", " << txkey.secretKey << ")";

        return false;
      }

      r = Crypto::derive_public_key(derivation, no, minerAddress.spendPublicKey, outEphemeralPubKey);

      if (!(r))
      {
        logger(ERROR, BRIGHT_RED)
            << "while creating outs: failed to derive_public_key("
            << derivation << ", " << no << ", "
            << minerAddress.spendPublicKey << ")";

        return false;
      }

      KeyOutput tk;
      tk.key = outEphemeralPubKey;

      TransactionOutput out;
      summaryAmounts += out.amount = outAmounts[no];
      out.target = tk;
      tx.outputs.push_back(out);
    }

    if (!(summaryAmounts == blockReward))
    {
      logger(ERROR, BRIGHT_RED) << "Failed to construct miner tx, summaryAmounts = " << summaryAmounts << " not equal blockReward = " << blockReward;
      return false;
    }

    tx.version = TRANSACTION_VERSION_1;
    // lock
    tx.unlockTime = height + m_minedMoneyUnlockWindow;
    tx.inputs.push_back(in);
    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::isFusionTransaction(const std::vector<uint64_t> &inputsAmounts, const std::vector<uint64_t> &outputsAmounts, size_t size) const
  {
    if (size > fusionTxMaxSize())
    {
      return false;
    }

    if (inputsAmounts.size() < fusionTxMinInputCount())
    {
      return false;
    }

    if (inputsAmounts.size() < outputsAmounts.size() * fusionTxMinInOutCountRatio())
    {
      return false;
    }

    uint64_t inputAmount = 0;
    for (auto amount : inputsAmounts)
    {
      if (amount < defaultDustThreshold())
      {
        return false;
      }

      inputAmount += amount;
    }

    std::vector<uint64_t> expectedOutputsAmounts;
    expectedOutputsAmounts.reserve(outputsAmounts.size());
    decomposeAmount(inputAmount, defaultDustThreshold(), expectedOutputsAmounts);
    std::sort(expectedOutputsAmounts.begin(), expectedOutputsAmounts.end());

    return expectedOutputsAmounts == outputsAmounts;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::isFusionTransaction(const Transaction &transaction, size_t size) const
  {
    assert(getObjectBinarySize(transaction) == size);

    std::vector<uint64_t> outputsAmounts;
    outputsAmounts.reserve(transaction.outputs.size());
    for (const TransactionOutput &output : transaction.outputs)
    {
      outputsAmounts.push_back(output.amount);
    }

    return isFusionTransaction(getInputsAmounts(transaction), outputsAmounts, size);
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::isFusionTransaction(const Transaction &transaction) const
  {
    return isFusionTransaction(transaction, getObjectBinarySize(transaction));
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::isAmountApplicableInFusionTransactionInput(uint64_t amount, uint64_t threshold, uint32_t height) const
  {
    uint8_t ignore;
    return isAmountApplicableInFusionTransactionInput(amount, threshold, ignore, height);
  }

  bool Currency::isAmountApplicableInFusionTransactionInput(uint64_t amount, uint64_t threshold, uint8_t &amountPowerOfTen, uint32_t height) const
  {
    if (amount >= threshold)
    {
      return false;
    }

    if (amount < defaultDustThreshold())
    {
      return false;
    }

    auto it = std::lower_bound(PRETTY_AMOUNTS.begin(), PRETTY_AMOUNTS.end(), amount);
    if (it == PRETTY_AMOUNTS.end() || amount != *it)
    {
      return false;
    }

    amountPowerOfTen = static_cast<uint8_t>(std::distance(PRETTY_AMOUNTS.begin(), it) / 9);
    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  std::string Currency::accountAddressAsString(const AccountBase &account) const
  {
    return getAccountAddressAsStr(m_publicAddressBase58Prefix, account.getAccountKeys().address);
  }

  /* ---------------------------------------------------------------------------------------------------- */

  std::string Currency::accountAddressAsString(const AccountPublicAddress &accountPublicAddress) const
  {
    return getAccountAddressAsStr(m_publicAddressBase58Prefix, accountPublicAddress);
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::parseAccountAddressString(const std::string &str, AccountPublicAddress &addr) const
  {
    Prefix prefix;
    if (!CryptoNote::parseAccountAddressString(prefix, addr, str))
    {
      return false;
    }

    if (prefix != m_publicAddressBase58Prefix)
    {
      logger(DEBUGGING) << "Wrong address prefix: " << prefix << ", expected " << m_publicAddressBase58Prefix;
      return false;
    }

    return true;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  std::string Currency::formatAmount(uint64_t amount) const
  {
    std::string s = std::to_string(amount);
    if (s.size() < m_numberOfDecimalPlaces + 1)
    {
      s.insert(0, m_numberOfDecimalPlaces + 1 - s.size(), '0');
    }

    s.insert(s.size() - m_numberOfDecimalPlaces, ".");
    return s;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  std::string Currency::formatAmount(int64_t amount) const
  {
    std::string s = formatAmount(static_cast<uint64_t>(std::abs(amount)));

    if (amount < 0)
    {
      s.insert(0, "-");
    }

    return s;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::parseAmount(const std::string &str, uint64_t &amount) const
  {
    std::string strAmount = str;
    boost::algorithm::trim(strAmount);

    size_t pointIndex = strAmount.find_first_of('.');
    size_t fractionSize;

    if (std::string::npos != pointIndex)
    {
      fractionSize = strAmount.size() - pointIndex - 1;
      while (m_numberOfDecimalPlaces < fractionSize && '0' == strAmount.back())
      {
        strAmount.erase(strAmount.size() - 1, 1);
        --fractionSize;
      }

      if (m_numberOfDecimalPlaces < fractionSize)
      {
        return false;
      }

      strAmount.erase(pointIndex, 1);
    }
    else
    {
      fractionSize = 0;
    }

    if (strAmount.empty())
    {
      return false;
    }

    if (!std::all_of(strAmount.begin(), strAmount.end(), ::isdigit))
    {
      return false;
    }

    if (fractionSize < m_numberOfDecimalPlaces)
    {
      strAmount.append(m_numberOfDecimalPlaces - fractionSize, '0');
    }

    return Common::fromString(strAmount, amount);
  }

  // LWMA-3 difficulty algorithm (commented version)
  // Copyright (c) 2017-2018 Zawy, MIT License
  // https://github.com/zawy12/difficulty-algorithms/issues/3

  // difficulty_type should be uint64_t
  difficulty_type Currency::LWMA3Difficulty(std::vector<std::uint64_t> timestamps, std::vector<difficulty_type> cumulative_difficulties) const
  {
    uint64_t T = 120; // target solvetime seconds
    uint64_t N = 60;  //  N=45, 60, and 90 for T=600, 120, 60.
    uint64_t L(0), ST, sum_3_ST(0), next_D, prev_D, this_timestamp, previous_timestamp;

    /* Dont use LWMA3 to calculate the genesis block */
    size_t length = timestamps.size();
    assert(length <= m_difficultyWindow);
    if (length <= 1) { return 1; }

    // Make sure timestamps & CD vectors are not bigger than they are supposed to be.
    assert(timestamps.size() == cumulative_difficulties.size() &&
           timestamps.size() <= N + 1);

    // If it's a new coin, do startup code.
    // Increase difficulty_guess if it needs to be much higher, but guess lower than lowest guess.
    uint64_t difficulty_guess = 100;
    if (timestamps.size() <= 10) {
      return difficulty_guess;
    }
    // Use "if" instead of "else if" in case vectors are incorrectly N all the time instead of N+1.
    if (timestamps.size() < N + 1) {
      N = timestamps.size() - 1;
    }

    // If hashrate/difficulty ratio after a fork is < 1/3 prior ratio, hardcode D for N+1 blocks after fork.
    // difficulty_guess = 100; //  Dev may change.  Guess low.
    // if (height <= UPGRADE_HEIGHT + N+1 ) { return difficulty_guess;  }

    // N is most recently solved block.
    previous_timestamp = timestamps[0];
    for (uint64_t i = 1; i <= N; i++) {
      // prevent out-of-sequence timestamps in a way that prevents
      // an exploit caused by "if ST< 0 then ST = 0"
      if (timestamps[i] > previous_timestamp) {
        this_timestamp = timestamps[i];
      } else {
        this_timestamp = previous_timestamp + 1;
      }
      // Limit solvetime ST to 6*T to prevent large drop in difficulty that could cause oscillations.
      ST = std::min(6 * T, this_timestamp - previous_timestamp);
      previous_timestamp = this_timestamp;
      L += ST * i; // give linearly higher weight to more recent solvetimes
                   // delete the following line if you do not want the "jump rule"
      if (i > N - 3) {
        sum_3_ST += ST;
      } // used below to check for hashrate jumps
    }
    // Calculate next_D = avgD * T / LWMA(STs) using integer math

    next_D = ((cumulative_difficulties[N] - cumulative_difficulties[0]) * T * (N + 1) * 99) / (100 * 2 * L);

    prev_D = cumulative_difficulties[N] - cumulative_difficulties[N - 1];
    // The following is only for safety to limit unexpected extreme events.
    next_D = std::max((prev_D * 67) / 100, std::min(next_D, (prev_D * 150) / 100));

    // If last 3 solvetimes were so fast it's probably a jump in hashrate, increase D 8%.
    // delete the following line if you do not want the "jump rule"
    if (sum_3_ST < (8 * T) / 10)
    {
      next_D = std::max(next_D, (prev_D * 108) / 100);
    }

    return next_D;

    // next_Target = sumTargets*L*2/0.998/T/(N+1)/N/N; // To show the difference.
  }

  /* ---------------------------------------------------------------------------------------------------- */

  bool Currency::checkProofOfWork(Crypto::cn_context &context, const Block &block, difficulty_type currentDifficulty,
                                  Crypto::Hash &proofOfWork) const
  {

    if (!get_block_longhash(context, block, proofOfWork))
    {
      return false;
    }

    return check_hash(proofOfWork, currentDifficulty);
  }

  /* ---------------------------------------------------------------------------------------------------- */

  size_t Currency::getApproximateMaximumInputCount(size_t transactionSize, size_t outputCount, size_t mixinCount) const
  {
    const size_t KEY_IMAGE_SIZE = sizeof(Crypto::KeyImage);
    const size_t OUTPUT_KEY_SIZE = sizeof(decltype(KeyOutput::key));
    const size_t AMOUNT_SIZE = sizeof(uint64_t) + 2;                   // varint
    const size_t GLOBAL_INDEXES_VECTOR_SIZE_SIZE = sizeof(uint8_t);    // varint
    const size_t GLOBAL_INDEXES_INITIAL_VALUE_SIZE = sizeof(uint32_t); // varint
    const size_t GLOBAL_INDEXES_DIFFERENCE_SIZE = sizeof(uint32_t);    // varint
    const size_t SIGNATURE_SIZE = sizeof(Crypto::Signature);
    const size_t EXTRA_TAG_SIZE = sizeof(uint8_t);
    const size_t INPUT_TAG_SIZE = sizeof(uint8_t);
    const size_t OUTPUT_TAG_SIZE = sizeof(uint8_t);
    const size_t PUBLIC_KEY_SIZE = sizeof(Crypto::PublicKey);
    const size_t TRANSACTION_VERSION_SIZE = sizeof(uint8_t);
    const size_t TRANSACTION_UNLOCK_TIME_SIZE = sizeof(uint64_t);

    const size_t outputsSize = outputCount * (OUTPUT_TAG_SIZE + OUTPUT_KEY_SIZE + AMOUNT_SIZE);
    const size_t headerSize = TRANSACTION_VERSION_SIZE + TRANSACTION_UNLOCK_TIME_SIZE + EXTRA_TAG_SIZE + PUBLIC_KEY_SIZE;
    const size_t inputSize = INPUT_TAG_SIZE + AMOUNT_SIZE + KEY_IMAGE_SIZE + SIGNATURE_SIZE + GLOBAL_INDEXES_VECTOR_SIZE_SIZE +
                             GLOBAL_INDEXES_INITIAL_VALUE_SIZE + mixinCount * (GLOBAL_INDEXES_DIFFERENCE_SIZE + SIGNATURE_SIZE);

    return (transactionSize - headerSize - outputsSize) / inputSize;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  CurrencyBuilder::CurrencyBuilder(Logging::ILogger &log) : m_currency(log)
  {
    maxBlockNumber(parameters::MAX_BLOCK_NUMBER);
    maxBlockBlobSize(parameters::MAX_BLOCK_BLOB_SIZE);
    maxTxSize(parameters::MAX_TX_SIZE);
    publicAddressBase58Prefix(parameters::PUBLIC_ADDRESS_BASE58_PREFIX);
    minedMoneyUnlockWindow(parameters::MINED_COINS_UNLOCK_WINDOW);

    timestampCheckWindow(parameters::BLOCKCHAIN_TIMESTAMP_CHECK_WINDOW);
    blockFutureTimeLimit(parameters::BLOCK_FUTURE_TIME_LIMIT);

    moneySupply(parameters::MONEY_SUPPLY);
    //genesisBlockReward(parameters::GENESIS_BLOCK_REWARD);

    rewardBlocksWindow(parameters::REWARD_BLOCKS_WINDOW);

    blockGrantedFullRewardZone(parameters::BLOCK_GRANTED_FULL_REWARD_ZONE);
    minerTxBlobReservedSize(parameters::COINBASE_BLOB_RESERVED_SIZE);

    numberOfDecimalPlaces(parameters::DECIMAL_POINT);

    minimumFee(parameters::MINIMUM_FEE);
    minimumFeeBanking(parameters::MINIMUM_FEE_BANKING);
    defaultDustThreshold(parameters::DEFAULT_DUST_THRESHOLD);

    difficultyTarget(parameters::DIFFICULTY_TARGET);
    difficultyWindow(parameters::DIFFICULTY_WINDOW);

    depositMinAmount(parameters::DEPOSIT_MIN_AMOUNT);
    depositMinTerm(parameters::DEPOSIT_MIN_TERM);
    depositMaxTerm(parameters::DEPOSIT_MAX_TERM);
    depositMinTotalRateFactor(parameters::DEPOSIT_MIN_TOTAL_RATE_FACTOR);
    depositMaxTotalRate(parameters::DEPOSIT_MAX_TOTAL_RATE);

    maxBlockSizeInitial(parameters::MAX_BLOCK_SIZE_INITIAL);
    maxBlockSizeGrowthSpeedNumerator(parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_NUMERATOR);
    maxBlockSizeGrowthSpeedDenominator(parameters::MAX_BLOCK_SIZE_GROWTH_SPEED_DENOMINATOR);

    lockedTxAllowedDeltaSeconds(parameters::LOCKED_TX_ALLOWED_DELTA_SECONDS);
    lockedTxAllowedDeltaBlocks(parameters::LOCKED_TX_ALLOWED_DELTA_BLOCKS);

    mempoolTxLiveTime(parameters::MEMPOOL_TX_LIVETIME);
    mempoolTxFromAltBlockLiveTime(parameters::MEMPOOL_TX_FROM_ALT_BLOCK_LIVETIME);
    numberOfPeriodsToForgetTxDeletedFromPool(parameters::NUMBER_OF_PERIODS_TO_FORGET_TX_DELETED_FROM_POOL);

    upgradeHeightV2(parameters::UPGRADE_HEIGHT_V2);
    upgradeVotingThreshold(parameters::UPGRADE_VOTING_THRESHOLD);
    upgradeVotingWindow(parameters::UPGRADE_VOTING_WINDOW);
    upgradeWindow(parameters::UPGRADE_WINDOW);

    transactionMaxSize(parameters::MAX_TX_SIZE_LIMIT);
    fusionTxMaxSize(parameters::FUSION_TX_MAX_SIZE);
    fusionTxMinInputCount(parameters::FUSION_TX_MIN_INPUT_COUNT);
    fusionTxMinInOutCountRatio(parameters::FUSION_TX_MIN_IN_OUT_COUNT_RATIO);

    blocksFileName(parameters::CRYPTONOTE_BLOCKS_FILENAME);
    blocksCacheFileName(parameters::CRYPTONOTE_BLOCKSCACHE_FILENAME);
    blockIndexesFileName(parameters::CRYPTONOTE_BLOCKINDEXES_FILENAME);
    txPoolFileName(parameters::CRYPTONOTE_POOLDATA_FILENAME);
    blockchinIndicesFileName(parameters::CRYPTONOTE_BLOCKCHAIN_INDICES_FILENAME);

    testnet(false);
  }

  /* ---------------------------------------------------------------------------------------------------- */

  Transaction CurrencyBuilder::generateGenesisTransaction()
  {
    CryptoNote::Transaction tx;
    CryptoNote::AccountPublicAddress ac = boost::value_initialized<CryptoNote::AccountPublicAddress>();
    m_currency.constructMinerTx(0, 0, 0, 0, 0, ac, tx); // zero fee in genesis

    return tx;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  CurrencyBuilder &CurrencyBuilder::numberOfDecimalPlaces(size_t val)
  {
    m_currency.m_numberOfDecimalPlaces = val;
    m_currency.m_coin = 1;
    for (size_t i = 0; i < m_currency.m_numberOfDecimalPlaces; ++i)
    {
      m_currency.m_coin *= 10;
    }

    return *this;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  CurrencyBuilder &CurrencyBuilder::difficultyWindow(size_t val)
  {
    if (val < 2)
    {
      throw std::invalid_argument("val at difficultyWindow()");
    }

    m_currency.m_difficultyWindow = val;
    return *this;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  CurrencyBuilder &CurrencyBuilder::upgradeVotingThreshold(unsigned int val)
  {
    if (val <= 0 || val > 100)
    {
      throw std::invalid_argument("val at upgradeVotingThreshold()");
    }

    m_currency.m_upgradeVotingThreshold = val;
    return *this;
  }

  /* ---------------------------------------------------------------------------------------------------- */

  CurrencyBuilder &CurrencyBuilder::upgradeWindow(uint32_t val)
  {
    if (val <= 0)
    {
      throw std::invalid_argument("val at upgradeWindow()");
    }

    m_currency.m_upgradeWindow = val;
    return *this;
  }

} // namespace CryptoNote
