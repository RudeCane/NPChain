# On-Chain Governance

## Democratic Decision-Making

NPChain uses on-chain governance where every Cert holder has a voice. Your voting power equals your Certs balance — the more you mine, the more influence you have.

## Voting Requirements

| Requirement | Value | Why |
|------------|-------|-----|
| **Minimum Participation** | **70%** of all Certs must vote | Prevents silent takeovers |
| **Approval Threshold** | **75%** of votes must approve | Ensures supermajority consensus |
| **Emergency Threshold** | **90%** approval required | Critical changes need near-unanimity |
| **Minimum to Propose** | ~47,564 Certs (1 block reward) | Prevents spam proposals |

## Why 70% Participation?

Many blockchains allow governance votes to pass with tiny participation — sometimes as low as 5-10% of token holders. This means a small whale or coordinated group can change the rules while everyone else is unaware.

NPChain requires 70% of all mined Certs to participate before any vote counts. This means:

- **No silent takeovers** — you can't sneak a vote through
- **No whale-only governance** — broad participation required
- **The whole network must engage** — changes reflect true community will
- **Changes reflect real consensus** — not just the loudest voices

## How Voting Works

### 1. Submit a Proposal

Anyone with at least ~47,564 Certs (one block reward) can submit a governance proposal. Proposals include a title, description, type, and for parameter changes, the specific parameter and new value.

### 2. Voting Period

Proposals are open for voting for 240 blocks (~1 hour on testnet). During this period, any Cert holder can vote:

- **Approve** — support the proposal
- **Reject** — oppose the proposal
- **Abstain** — participate without taking a side

Your voting power equals your Certs balance. If you hold 500,000 Certs, your vote carries 500,000 weight.

### 3. Vote Counting

When the voting period ends:

- If **less than 70%** of total supply participated → proposal **expires** (no quorum)
- If **70%+ participated** and **75%+ approved** → proposal **passes**
- If **70%+ participated** and **less than 75% approved** → proposal **rejected**

### 4. Activation Delay

Passed proposals don't activate immediately. There is a 120-block delay (~30 minutes on testnet) giving all nodes time to update their software.

### 5. Emergency Proposals

Security vulnerabilities can be fast-tracked with:
- Shorter voting period (40 blocks / ~10 minutes)
- Higher approval requirement (90%)
- Higher participation requirement (50%)

## Proposal Types

| Type | What It Changes |
|------|----------------|
| **Parameter Change** | Block time, difficulty window, consensus parameters |
| **Crypto Upgrade** | Add or deprecate cryptographic algorithms |
| **Fee Change** | Transaction fee structure |
| **Emission Change** | Block reward, emission schedule |
| **General** | Any other governance decision |

## How to Vote

1. Open **http://localhost:8888/governance.html** in your browser
2. Enter your wallet address (same one you mine with)
3. Browse active proposals
4. Click **Approve**, **Reject**, or **Abstain**
5. Your vote weight = your Certs balance

## How to Submit a Proposal

1. Open the Governance page
2. Click the **Submit Proposal** tab
3. Select proposal type
4. Write a clear title and description
5. For parameter changes, specify the parameter key and new value
6. Submit — requires ~47,564 Certs minimum balance

## RPC Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/governance/proposals` | GET | List all proposals |
| `/api/v1/governance/proposal/N` | GET | Get proposal #N |
| `/api/v1/governance/submit` | POST | Submit a proposal |
| `/api/v1/governance/vote` | POST | Cast a vote |
| `/api/v1/governance/config` | GET | Governance parameters |
