// Copyright © Earth Heroes 2024. Defend The Dungeon™ is a trademark of Earth Heroes. All Rights Reserved.


#include "CombatComponent.h"

#include "Ability/Effect/BarrierEffect.h"
#include "Ability/Effect/GuardEffect.h"
#include "Ability/Effect/StealthHeistEffect.h"
#include "Camera/CameraComponent.h"
#include "Component/Projectile/ProjectileShooterComponent.h"
#include "DefendTheDungeon/Ability/Effect/DamageEffect.h"
#include "DefendTheDungeon/Ability/Effect/EventBuffEffect.h"
#include "DefendTheDungeon/Ability/Effect/SlowEffect.h"
#include "DefendTheDungeon/Ability/StatComponent/CharacterStatComponent.h"
#include "DefendTheDungeon/Ability/StatComponent/MonsterStatComponent.h"
#include "DefendTheDungeon/Actor/Damageable/DamageableActor.h"
#include "DefendTheDungeon/Character/DDCharacter.h"
#include "DefendTheDungeon/Character/Monster/MonsterBase.h"
#include "DefendTheDungeon/ETC/CustomMacro.h"
#include "DefendTheDungeon/PlayerController/IngamePlayerController.h"
#include "DefendTheDungeon/Skill/MagicProjectile/GravityProjectile.h"
#include "DefendTheDungeon/Skill/SpawnSkill/DarkMagicOrbSkill.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Skill/SkillActorHaveStatComp.h"
#include "UI/CombatWidget/SkillWidget.h"
#include "UI/HUD/W_IngameHUD.h"

// Sets default values for this component's properties
UCombatComponent::UCombatComponent(): DDCharacter(nullptr)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	SkillAnimMontage.SetNum(3);
	DashAnimMontage.SetNum(8);
	
	const FSoftObjectPath SoftObjectPath("/Game/Effects/InfinityBladeEffects/Effects/FX_Ability/Stun/P_Stun_Stars_Base.P_Stun_Stars_Base");
	if(UObject* LoadedObject = SoftObjectPath.TryLoad())
	{
		StunParticle = Cast<UParticleSystem>(LoadedObject);
	}

	static ConstructorHelpers::FObjectFinder<UAnimMontage> StunMontageClass(TEXT("/Game/Character/Animation/NoWeapon/Dizzy_Anim_Montage.Dizzy_Anim_Montage"));
	if (StunMontageClass.Succeeded())
	{
		StunMontage = StunMontageClass.Object;
	}

	//CurAction Initialize
	CurAction.Owner = nullptr;
	CurAction.ActionLevel = ActionMax;
	CurAction.CancelLevel = ActionMax;
}

void UCombatComponent::SetIngameplayerController_Implementation()
{
	if (!DDCharacter) LOG_RETURN(Error, TEXT("No DDCharacter"));
	check(DDCharacter);

	if (AIngamePlayerController *PlayerController = Cast<AIngamePlayerController>(DDCharacter->GetController()))
	{
		IngamePlayerController = PlayerController;
	}
	else LOG_RETURN(Error, TEXT("Failed To Get Controllerr"));

	check(IngamePlayerController);
}

bool UCombatComponent::CanEquipItem() const
{
	if (!CanPlayAction(5)) return false;
	
	return true;
}

void UCombatComponent::OnDamaged(AActor* InInstigator, float Damage, EAttackType DamageAttackType)
{
	//MY_LOG(LogTemp, Error, TEXT("Damage = %f"), Damage)
	if (Damage > 0.f)
	{
		if (bStealthed) EndStealth();
	}
}

bool UCombatComponent::CanPlayAction(int32 ActionLevel) const
{
	return ActionLevel <= CurAction.CancelLevel; // 더 높은 우선순위면 true
}

bool UCombatComponent::CheckValidAction(const FAction& Action) const
{
	if (!Action.Owner->FindFunction(Action.PlayFunctionName))
	{
		MY_LOG(LogTemp, Error, TEXT("PlayFunction is Not Valid, Check Owener Member Function.  Owner %s, ActionName %s, ActionType %s"), *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		return false;
	}

	if (!Action.CancelFunctionName.IsNone() && !Action.Owner->FindFunction(Action.CancelFunctionName))
	{
		MY_LOG(LogTemp, Error, TEXT("CancelFunction is Not Valid, Check Owener Member Function, Owner %s, ActionName %s, ActionType %s"), *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		return false;
	}

	if (!Action.EndFunctionName.IsNone() && !Action.Owner->FindFunction(Action.EndFunctionName))
	{
		MY_LOG(LogTemp, Error, TEXT("EndFunction is Not Valid, Check Owener Member Function, Owner %s, ActionName %s, ActionType %s"), *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		return false;
	}

	return true;
}

bool UCombatComponent::TryPlayAction_Internal(FAction& Action)
{
	if (GetNetMode() != NM_ListenServer)
	{
		MY_LOG(LogTemp, Error, TEXT("TryPlayAction Called in Client"));
		return false;
	}
	
	if (!CheckValidAction(Action)) return false;

	if (Action.ActionLevel < 0) Action.ActionLevel = 0;
	else if (Action.ActionLevel > ActionMax) Action.ActionLevel = ActionMax;

	if (Action.CancelLevel < 0) Action.CancelLevel = 0;
	else if (Action.CancelLevel > ActionMax) Action.CancelLevel = ActionMax;

	if (!CanPlayAction(Action.ActionLevel))
	{
		MY_LOG(LogTemp, Log, TEXT("Try Action Level %d < Cur Cancel Level %d, denied. Owner %s, ActionName %s, ActionType %s"), Action.ActionLevel, CurAction.CancelLevel, *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		return false;
	}

	//전 액션과 동일한 경우, 건너뛴다.
	if (CurAction != Action)
	{
		//이전 액션 취소 함수 호출
		if (ActionCancelCalled.ExecuteIfBound()) ActionCancelCalled.Clear();

		//취소 함수 바인딩
		if (!Action.CancelFunctionName.IsNone())
			ActionCancelCalled.BindUFunction(Action.Owner, Action.CancelFunctionName);

		//취소 함수가 비워져 있을 땐, 기본 상태로 돌아간다 생각한다.
		else
		{
			ActionEnded.BindUFunction(this, "SetDefaultAction");
		}

		//엔드 함수 바인딩
		ActionEnded.Clear();
		if (!Action.EndFunctionName.IsNone())
			ActionEnded.BindUFunction(Action.Owner, Action.EndFunctionName);

		//엔드 함수가 비워져 있을 땐, 기본 상태로 돌아간다 생각한다.
		else
		{
			ActionEnded.BindUFunction(this, "SetDefaultAction");
		}
	
		//새로운 액션 바인딩
		ActionCalled.Clear();
		ActionCalled.BindUFunction(Action.Owner, Action.PlayFunctionName);
	}

	//액션 실행
	if (ActionCalled.ExecuteIfBound())
	{
		MY_LOG(LogTemp, Log, TEXT("Action Called, Owner %s, ActionName %s, ActionType %s"), *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		CurAction = Action;
	}
	else
	{
		MY_LOG(LogTemp, Warning, TEXT("Action Call Failed, Owner %s, ActionName %s, ActionType %s"), *GetNameSafe(Action.Owner), *Action.ActionName.ToString(), *UEnum::GetValueAsString(Action.ActionType));
		return false;
	}
	
	return true;
}

void UCombatComponent::Server_TryPlayAction_Implementation(FAction Action)
{
	TryPlayAction_Internal(Action);
}

void UCombatComponent::TryPlayAction(FAction& Action)
{
	TryPlayAction_Internal(Action);
}

void UCombatComponent::TryPlayAction(UObject *Owner, FName PlayFunctionName, int32 ActionLevel, int32 CancelLevel, FName EndFunctionName, FName CancelFunctionName, EActionType ActionType, FName ActionName)
{
	if (!IsValid(Owner)) return;
	
	FAction Action(Owner, ActionType, PlayFunctionName, CancelFunctionName, EndFunctionName, ActionLevel, CancelLevel);

	if (!ActionName.IsNone())
		Action.ActionName = ActionName;

	TryPlayAction_Internal(Action);
}

void UCombatComponent::ForcePlayAction(FAction& Action)
{
	Action.ActionLevel = 0;
	TryPlayAction(Action);
}

bool UCombatComponent::Attack()
{
	if (AttackAnimMontage.Num() <= 0 || !IsValid(AttackAnimMontage[0]))
	{
		MY_LOG(LogTemp, Error, TEXT("No Attack Montage"));
		return false;
	}

	if (bIsAttacking) return false;

	FAction AttackAction(this, Attacking, FName("Attack_Action"), FName("Attack_Cancel"), FName("AttackEnd"), 4, 3, FName("Attack"));
	Server_TryPlayAction(AttackAction);
	
	return true;
}


void UCombatComponent::AttackEnd()
{
	bIsAttacking = false;
	SetDefaultAction();
}


void UCombatComponent::Dash()
{
	if (!bDashReady) return;
	
	APlayerController* PlayerController = Cast<APlayerController>(DDCharacter->GetController());
	
	if (PlayerController->IsInputKeyDown(EKeys::A) && PlayerController->IsInputKeyDown(EKeys::W))
	{
		DashSide = ENoWeaponDash::FrontLeft;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::D) && PlayerController->IsInputKeyDown(EKeys::W))
	{
		DashSide = ENoWeaponDash::FrontRight;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::A) && PlayerController->IsInputKeyDown(EKeys::S))
	{
		DashSide = ENoWeaponDash::BackLeft;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::D) && PlayerController->IsInputKeyDown(EKeys::S))
	{
		DashSide = ENoWeaponDash::BackRight;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::A))
	{
		DashSide = ENoWeaponDash::Left;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::D))
	{
		DashSide = ENoWeaponDash::Right;
	}
	else if(PlayerController->IsInputKeyDown(EKeys::W))
	{
		DashSide = ENoWeaponDash::Front;
	}
	else if (PlayerController->IsInputKeyDown(EKeys::S))
	{
		DashSide = ENoWeaponDash::Back;
	}
	else
	{
		DashSide = ENoWeaponDash::Front;
	}

	Server_Dash(DashSide);
}

void UCombatComponent::Server_Dash_Implementation(ENoWeaponDash InDashSide)
{
	DashSide = InDashSide;
	
	FAction DashAction(this, EActionType::Skill, FName("Dash_Action"), FName("Dash_Cancel"), FName("DashEnd"), 3, 2);
	TryPlayAction(DashAction);

}

void UCombatComponent::MC_Dash_Implementation(ENoWeaponDash DashDirection)
{
	FName SectionName = FName();
	SectionName = FName("Roll");
	
	//MY_LOG(LogTemp, Warning, TEXT("SectionName : %s"), *SectionName.ToString());
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();

	UGameplayStatics::SpawnSoundAttached(DDCharacter->DashSound, DDCharacter->GetRootComponent(), "root");
	
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)
	{
		uint8 DashIndex = static_cast<uint8>(DashDirection);
		AnimInstance->Montage_Play(DashAnimMontage[DashIndex]);
		AnimInstance->Montage_JumpToSection(SectionName, DashAnimMontage[DashIndex]);
	}
}


bool UCombatComponent::SkillQ()
{
	if (SkillAnimMontage.Num() <= 0 || !IsValid(SkillAnimMontage[Skill_Q]))
	{
		MY_LOG(LogTemp, Error, TEXT("No SkillQ Montage"));
		return false;
	}

	if (!bQReady) return false;

	FAction SkillQAction(this, Skill, FName("SkillQ_Action"), FName("SkillQ_Cancel"), FName("SkillQEnd"), 3, 2, FName("SkillQ"));
	Server_TryPlayAction(SkillQAction);
	
	return true;
}

//E 스킬 사용 시
bool UCombatComponent::SkillE()
{
	if (SkillAnimMontage.Num() <= 0 || !IsValid(SkillAnimMontage[Skill_E]))
	{
		MY_LOG(LogTemp, Error, TEXT("No SkillE Montage"));
		return false;
	}

	if (!bEReady) return false;
	
	FAction SkillEAction(this, Skill, FName("SkillE_Action"), FName("SkillE_Cancel"), FName("SkillEEnd"), 3, 2, FName("SkillE"));
	Server_TryPlayAction(SkillEAction);
	
	return true;
}

bool UCombatComponent::SkillR()
{
	if (SkillAnimMontage.Num() <= 0 || !IsValid(SkillAnimMontage[Skill_R]))
	{
		MY_LOG(LogTemp, Error, TEXT("No SkillR Montage"));
		return false;
	}

	if (!bRReady) return false;

	FAction SkillRAction(this, Skill, FName("SkillR_Action"), FName("SkillR_Cancel"), FName("SkillREnd"), 3, 2, FName("SkillR"));
	Server_TryPlayAction(SkillRAction);
	
	return true;
}

void UCombatComponent::MC_PlayAttackMontage_Implementation(int32 CurComboCount)
{
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)           
	{
		FMontageBlendSettings BlendSettings(0.f);
		AnimInstance->Montage_PlayWithBlendSettings(AttackAnimMontage[0], 0.f, AttackSpeed);
	}
}

void UCombatComponent::DetectedHit()
{
}
void UCombatComponent::SkillQDetectedHit()
{
}
void UCombatComponent::SkillEDetectedHit()
{
}
void UCombatComponent::SkillRDetectedHit()
{
}

void UCombatComponent::SubWeaponSkill()
{
	switch (DDCharacter->SubWeaponMode)
	{
	case ESubWeaponMode::Sub_NoWeapon :
		break;
	case ESubWeaponMode::Sub_Shield :
		ShieldProvocation();
		break;
	case ESubWeaponMode::Sub_Sword :
		SwordHiding();
		break;
	case ESubWeaponMode::Sub_MagicOrb :
		GiveShieldReady();
		break;
	case ESubWeaponMode::Sub_MagicWand :
		GravityProjectile();
		break;
	case ESubWeaponMode::Sub_DarkMagicOrb :
		DarkMagicOrbSkill();
		break;
	default:
		MY_LOG(LogTemp, Error, TEXT("Invalid SubWeaponMode"));
	}
}

void UCombatComponent::DashEnd()
{
	bIsDashing = false;
	StartDashCoolTime();
	SetDefaultAction();
}

void UCombatComponent::SkillQEnd()
{
	bSkillQ = false;
	StartQCoolTime();
	SetDefaultAction();
}

void UCombatComponent::SkillEEnd()
{
	bSkillE = false;
	StartECoolTime();
	SetDefaultAction();
}

void UCombatComponent::SkillREnd()
{
	bSkillR = false;
	StartRCoolTime();
	SetDefaultAction();
}

void UCombatComponent::ShootProjectile()
{
}

void UCombatComponent::Client_TickUpdate()
{
}

void UCombatComponent::Client_TickUpdateEnd()
{
}

void UCombatComponent::SkillR_KeyDown()
{
}

void UCombatComponent::SkillQ_Confirm()
{
}

void UCombatComponent::Skill_Confirm(int SkillCommand)
{
	if (bSkillE && (SkillCommand == Skill_E || SkillCommand == ESkillCommand::Attack))
	{
		Server_Skill_Confirm_SubWeapon();
	}
	else
	{
		//MY_LOG(LogTemp, Log, TEXT("bSkillE = %s, SkillCommand = %d"), bSkillE ? TEXT("true") : TEXT("false"), SkillCommand);
	}
}

void UCombatComponent::Server_Skill_Confirm_SubWeapon_Implementation()
{
	if (bSkillE)
	{
		if (bAimingToGiveShield)
		{
			StartECoolTime();
			bAimingToGiveShield = false;
			MC_ConfirmGiveShield();
			GiveShield();
			SetComponentTick(false);
			SkillEEnd();
		}
		else if (bDarkMagicOrbSkill)
		{
			bDarkMagicOrbSkill = false;
			MC_ConfirmGiveShield();
			StartECoolTime();
			SetComponentTick(false);

			if (DecalMagicOrbSkill)
			{
				DecalMagicOrbSkill->SpawnNS();
			}

			GetWorld()->GetTimerManager().SetTimer(DarkMagicOrbHandle, this, &UCombatComponent::DarkMagicOrbSkillRun, 0.2f, true);
			SkillEEnd();
		}
		else if (bGravityProjectileShooted && IsValid(ShootedGravityProjectile))
		{
			//MY_LOG(LogTemp, Error, TEXT("다시 눌러봄"));
			ShootedGravityProjectile->Destroy();
			ShootedGravityProjectile = nullptr;
		}
	}
	else
	{
		//MY_LOG(LogTemp, Log, TEXT("Skill_Confirm but No bSkillE"));
	}
}

void UCombatComponent::MC_ConfirmGiveShield_Implementation()
{
	UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		// if (GetNetMode() == NM_ListenServer)
		// 	MY_LOG(LogTemp, Error, TEXT("Jump to Section in Server"));
		AnimInstance->Montage_JumpToSection(TEXT("End"));
	}
}

void UCombatComponent::Attack_Cancel()
{
	bIsAttacking = false;
	SetDefaultAction();
}

void UCombatComponent::SkillQ_Cancel()
{
	bSkillQ = false;
	StartQCoolTime();
	SetDefaultAction();
}

void UCombatComponent::SkillE_Cancel()
{
	bSkillE = false;
	//보조무기 중단 추가
	if (bAimingToGiveShield)
	{
		StartECoolTime();
		bAimingToGiveShield = false;
		SetComponentTick(false);
	}
	else if (bDarkMagicOrbSkill)
	{
		bDarkMagicOrbSkill = false;
		StartECoolTime();
		SetComponentTick(false);

		if (DecalMagicOrbSkill)
		{
			DecalMagicOrbSkill->K2_DestroyActor();
			DecalMagicOrbSkill = nullptr;
		}
	}
	else if (bGravityProjectileShooted && IsValid(ShootedGravityProjectile))
	{
		//MY_LOG(LogTemp, Error, TEXT("다시 눌러봄"));
		ShootedGravityProjectile->Destroy();
		ShootedGravityProjectile = nullptr;
	}

	SetDefaultAction();
}

void UCombatComponent::SkillR_Cancel()
{
	bSkillR = false;
	StartRCoolTime();
	SetDefaultAction();
}

void UCombatComponent::Dash_Cancel()
{
	bIsDashing = false;
	StartDashCoolTime();
	SetDefaultAction();
}

void UCombatComponent::Block_Cancel()
{
	if(StatComponent)
	{
		UBaseEffect* PlayerGuardEffect = StatComponent->FindEffectByName("PlayerGuard");
		if(PlayerGuardEffect)
		{
			StatComponent->RemoveEffect(PlayerGuardEffect);
		}
	}
	
	bIsBlocking = false;
	StartBlockCoolTime();
	StopMontage(0.f, BlockMontage);
	SetDefaultAction();
}

void UCombatComponent::ResetBoolValByKnockbacked()
{
	bIsDashing = false;
	bIsAttacking = false;
	bSkillQ = false;
	bSkillE = false;
	bSkillR = false;
}

void UCombatComponent::StopMontage_Implementation(float BlendOut, UAnimMontage* Montage)
{
	if (UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance())
	{
		if (IsValid(Montage))
		{
			AnimInstance->Montage_Stop(BlendOut, Montage);
		}
	}
}

void UCombatComponent::StopAllMontages_Implementation()
{
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)           
	{
		AnimInstance->StopAllMontages(0);
	}
	if (DDCharacter->Bow)
	{
		UAnimInstance* BowAnimInstance = DDCharacter->Bow->GetAnimInstance();
		if (BowAnimInstance)           
		{
			BowAnimInstance->StopAllMontages(0);
		}
	}
	
	if (DDCharacter->ArrowMesh)
	{
		UAnimInstance* ArrowAnimInstance = DDCharacter->ArrowMesh->GetAnimInstance();
		if (ArrowAnimInstance)           
		{
			ArrowAnimInstance->StopAllMontages(0);
		}
	}
}

void UCombatComponent::ResetValue()
{
	ComboCount = 0;
	SkillComboCount = 0;
	bIsAttacking = false;
	bSkillQ = false;
	bSkillE = false;
	bSkillR = false;
	bIsBlocking = false;
	bQReady = true;
	bEReady = true;
	bRReady = true;
	bDashReady = true;
	bBlockReady = true;
	bIsDashing = false;
	bStunned = false;
	bShocked = false;

	if (IngamePlayerController)
	{
		IngamePlayerController->Client_BanSkillImage(false, true, true, true);
		IngamePlayerController->Client_SetStunState(false);
	}
	
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(QCoolTimeHandle);
		GetWorld()->GetTimerManager().ClearTimer(ECoolTimeHandle);
		GetWorld()->GetTimerManager().ClearTimer(RCoolTimeHandle);
	}
}

void UCombatComponent::SkillInterrupted(UAnimMontage *AnimMontage)
{
	if (bIsAttacking && AttackAnimMontage.IsValidIndex(ComboCount) && AnimMontage != AttackAnimMontage[ComboCount])
	{
		Attack_Cancel();
	}
	if (bSkillQ)
	{
		SkillQ_Cancel();
	}
	else if (bSkillE)
	{
		SkillE_Cancel();
	}
	else if (bSkillR)
	{
		SkillR_Cancel();
	}
}

void UCombatComponent::ShieldProvocation()
{
	//서버 단계에서 도발 코드
	//MY_LOG(LogTemp, Log, TEXT("Provacation Activated"));

	if (!DDCharacter) return;
	
	FVector ActorLocation = DDCharacter->GetActorLocation();
	float SearchRadius = 1500.0f;

	// 검색할 오브젝트 타입 설정 (몬스터는 보통 Pawn이나 PhysicsBody)
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_GameTraceChannel1));

	// 무시할 액터 (자기 자신)
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(DDCharacter);

	// 결과 저장할 배열
	TArray<AActor*> OverlappedActors;

	// Sphere Overlap 실행
	bool bHit = UKismetSystemLibrary::SphereOverlapActors(
		GetWorld(),
		ActorLocation,
		SearchRadius,
		ObjectTypes,
		AMonsterBase::StaticClass(), // AMonsterBase 클래스만 검색
		IgnoreActors,
		OverlappedActors
	);

	// 검색 결과 로그 출력
	if (bHit)
	{
		for (AActor* FoundActor : OverlappedActors)
		{
			AMonsterBase* Monster = Cast<AMonsterBase>(FoundActor);
			if (Monster)
			{
				Monster->SetProvocation(DDCharacter, 3);
			}
		}
	}

	StartECoolTime();
}


void UCombatComponent::SwordHiding()
{
	if (!bStealthed)
	{
		if (!DDCharacter) return;

		//캐릭터의 모든 머테리얼을 갖고와서 저장
		//은신 중에는 장비 교체 못하게 해야 함

		if(DDCharacter->WeaponMode == EWeaponMode::DoubleSword)
		{
			DDCharacter->GetCombatComponent()->SetStealth();
		}
		else
		{
			FVector ActorLocation = DDCharacter->GetActorLocation();
			float SearchRadius = 1000.0f;
		
			TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
			ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_GameTraceChannel2));

			// 무시할 액터 (자기 자신)
			TArray<AActor*> IgnoreActors;

			// 결과 저장할 배열
			TArray<AActor*> OverlappedActors;

			// Sphere Overlap 실행
			bool bHit = UKismetSystemLibrary::SphereOverlapActors(
				GetWorld(),
				ActorLocation,
				SearchRadius,
				ObjectTypes,
				ADDCharacter::StaticClass(),
				IgnoreActors,
				OverlappedActors
			);

			//MY_LOG(LogTemp, Warning, TEXT("Provacation Activated!, Num %d"), OverlappedActors.Num());
			// 검색 결과 로그 출력
			if (bHit)
			{
				for (AActor* FoundActor : OverlappedActors)
				{
					ADDCharacter* AddCharacter = Cast<ADDCharacter>(FoundActor);
					if (AddCharacter)
					{
						AddCharacter->GetCombatComponent()->SetStealth();
					}
				}
			}
		}
	}
	
	StartECoolTime();
}

void UCombatComponent::EndStealth()
{
	if(!GetOwner()->HasAuthority()) return;
	
	if (bStealthed)
	{
		UBaseEffect* HeistEffect = DDCharacter->GetStatComponent()->FindEffectByName("StealthHeist");
		DDCharacter->GetStatComponent()->RemoveEffect(HeistEffect);
		
		bStealthed = false;
		GetWorld()->GetTimerManager().ClearTimer(StealthHandle);
		MC_SetStealth(false);
	}
}

void UCombatComponent::SetStealth()
{
	if(!GetOwner()->HasAuthority()) return;
	
	if (!bStealthed)
	{
		// 속도 이펙트 추가. 현재 더블스워드라면 이동속도 증가량 60%로 상향
		UStealthHeistEffect* SpeedEffect = NewObject<UStealthHeistEffect>();
		float SpeedPercent = 0.3f;
		if(DDCharacter->WeaponMode == EWeaponMode::DoubleSword)
		{
			SpeedPercent *= 2.f;
		}
		SpeedEffect->Initialize(GetOwner(), SpeedPercent);
		DDCharacter->GetStatComponent()->ApplyEffect(SpeedEffect);
		
		bStealthed = true;
		MC_SetStealth(true);

		//캐릭터가 스텔스화 됐다는 것을 이 캐릭터를 감지한 몬스터들에게 전달
		if (DDCharacter->OnCharacterStealthed.IsBound())
		{
			DDCharacter->OnCharacterStealthed.Broadcast(DDCharacter);
		}
		GetWorld()->GetTimerManager().SetTimer(StealthHandle, this, &UCombatComponent::EndStealth, 10, false);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(StealthHandle, this, &UCombatComponent::EndStealth, 10, false);
	}
}

void UCombatComponent::GiveShieldReady()
{
	if (bSkillE)
	{
		bAimingToGiveShield = true;
		SetComponentTick(true);
	}
}

void UCombatComponent::SetComponentTick_Implementation(bool bTick)
{
	OverlayedCharacter = nullptr;
	SetComponentTickEnabled(bTick);
}


void UCombatComponent::MC_SetStealth_Implementation(bool bStealth)
{
	if (bStealth)
	{
		//MY_LOG(LogTemp, Error, TEXT("Stealth Activated!!"));
		VisibleMaterials = DDCharacter->GetDDCharacterMaterials();

		DDCharacter->SetDDCharacterMaterials(StealthMaterial);
	}
	else
	{
		//MY_LOG(LogTemp, Error, TEXT("Stealth DeActivated!!"));
		for (int32 i = 0; i < VisibleMaterials.Num(); i++)
		{
			DDCharacter->SetDDCharacterMaterial(i, VisibleMaterials[i]);
		}
	}
}

void UCombatComponent::GiveShield()
{
	float CurretHealth = DDCharacter->GetStatComponent()->GetCurrentValue(EAttributeType::MaxHealth);
	
	if (OverlayedCharacter)
	{
		CL_SetOverlayInst(false);
	}
	else //없다면 자신에게 주기
	{
		OverlayedCharacter = DDCharacter;
	}

	if(OverlayedCharacter)
	{
		ApplyBarrier(DDCharacter, CurretHealth * 0.3f);

		// 마법사일 때 추가 버프
		if(DDCharacter->WeaponMode == EWeaponMode::MagicWand)
		{
			OverlayedCharacter->GetStatComponent()->ApplyBuff(EBuffType::Up_MagicCrystal_Buff);
		}
		else
		{
			OverlayedCharacter->GetStatComponent()->ApplyBuff(EBuffType::MagicCrystal_Buff);

		}
	}
}

void UCombatComponent::ApplyBarrier(AActor* NewInstigator, float Amount)
{
	UBarrierEffect* BarrierEffect = NewObject<UBarrierEffect>();
	BarrierEffect->Initialize(NewInstigator, Amount);

	StatComponent->ApplyEffect(BarrierEffect);
}

void UCombatComponent::DarkMagicOrbSkill()
{
	if (bSkillE)
	{
		DecalMagicOrbSkill = GetWorld()->SpawnActor<ADarkMagicOrbSkill>(BP_MagicOrbDecal, FVector::ZeroVector, FRotator::ZeroRotator);
		if (IsValid(DecalMagicOrbSkill))
		{
			bDarkMagicOrbSkill = true;
			SetComponentTick(true);
		}
		else
		{
			MY_LOG(LogTemp, Warning, TEXT("Fail to Spawn DecalActor"));
		}

		StartECoolTime();
	}
}

void UCombatComponent::DarkMagicOrbSkillRun()
{
	FVector StartLocation = DecalLocation;
	FVector EndLocation = StartLocation;
	float SphereRadius = 250.0f;

	TArray<FHitResult> HitResults;
	bool bHit = SphereTrace(StartLocation, EndLocation, SphereRadius, HitResults);

	if (bHit)
	{
		for (FHitResult &HitResult : HitResults)
		{
			AActor *HitActor = HitResult.GetActor();
			//MY_LOG(LogTemp, Warning, TEXT("Monster Debuff : HitMonster = %s"), *GetNameSafe(HitActor));
			if (AMonsterBase *MonsterBase = Cast<AMonsterBase>(HitActor))
			{
				USlowEffect* SlowEffect = NewObject<USlowEffect>();
				//받는 데미지 증가 디버프 추가해야 함
				if (MonsterBase->GetMonsterRole() == EMonsterRole::Boss)
				{
					SlowEffect->Initialize(DDCharacter, "DarkMagicOrbSkill", 0.1f, 0.2f);
				}
				else
				{
					SlowEffect->Initialize(DDCharacter, "DarkMagicOrbSkill", 0.3f, 0.2f);
				}
				UMonsterStatComponent* MonsterStat = MonsterBase->GetStatComponent();
				if(MonsterStat)
				{
					MonsterStat->ApplyEffect(SlowEffect);
					MonsterStat->ApplyBuff(EBuffType::DD_IncreasedDamageReduce_10);
				}
			}
		}
	}
	else
	{
		MY_LOG(LogTemp, Log, TEXT("No Hit"));
	}

	
	//MY_LOG(LogTemp, Warning, TEXT("DarkMagicOrbTime = %f"), DarkMagicOrbTime);
	DarkMagicOrbTime += 0.2f;
	if (DarkMagicOrbTime >= 4.f)
	{
		GetWorld()->GetTimerManager().ClearTimer(DarkMagicOrbHandle);
		DarkMagicOrbTime = 0.f;
	}
}

void UCombatComponent::GravityProjectile()
{
	if (bSkillE)
	{
		//중력포 소환
		bGravityProjectileShooted = true;
		FVector SpawnLocation = DDCharacter->GetMesh()->GetSocketLocation(TEXT("SkillActorSpawn"));
		FRotator SpawnRotation = DDCharacter->GetController()->GetControlRotation();
		ShootedGravityProjectile = ProjectileShooterComp->GetPreparedDisabledProjectile<AGravityProjectile>(SpawnLocation, SpawnRotation, DDCharacter, EDDCharacterPoolingProjectileType::BP_GravityProjectile);
		if (ShootedGravityProjectile)
		{
			ShootedGravityProjectile->StatComponent = DDCharacter->GetStatComponent();
			ShootedGravityProjectile->SetOwner(DDCharacter);
			ShootedGravityProjectile->Fire(ShootedGravityProjectile->GetActorForwardVector(), 1000.f);
			GetWorld()->GetTimerManager().SetTimer(ShootedGravityProjectile->DestroyExploHandle, ShootedGravityProjectile, &AGravityProjectile::Destroy, 1, false);
		}
	}
}

void UCombatComponent::Stun(float Duration)
{
	if (!CanPlayAction(0)) return;
	
	StopAllMontages();
	SkillInterrupted(nullptr);
	ResetBoolValByKnockbacked();
	bStunned = true;
	MC_StunMontage(true);
	
	if (IngamePlayerController)
	{
		IngamePlayerController->Client_BanSkillImage(true, true, true, true);
		IngamePlayerController->Client_SetStunState(true);
	}


	FTimerDelegate TimerDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bStunned = false;
		MC_StunMontage(false);
		if (IngamePlayerController)
		{
			IngamePlayerController->Client_BanSkillImage(false, true, true, true);
			IngamePlayerController->Client_SetStunState(false);
		}
	});
	
	GetWorld()->GetTimerManager().SetTimer(StunTimerHandle, TimerDelegate, Duration, false);
}

void UCombatComponent::MC_StunMontage_Implementation(bool bInStunned)
{
	UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		if (bInStunned)
		{
			AnimInstance->Montage_Play(StunMontage);
		}
		else
		{
			AnimInstance->Montage_Stop(0.f, StunMontage);
		}
	}
	
	if (bInStunned)
	{
		if (!StunComp)
			StunComp = UGameplayStatics::SpawnEmitterAttached(StunParticle, DDCharacter->GetMesh(), "Stun");
	}
	else
	{
		if (StunComp)
		{
			StunComp->DestroyComponent();
			StunComp = nullptr;
		}
	}
}

void UCombatComponent::CL_SetbCanLook_Implementation(bool bNewCanLook)
{
	if (IngamePlayerController)
	{
		IngamePlayerController->bCanLook = bNewCanLook;
	}
}


void UCombatComponent::CL_SetOverlayInst_Implementation(bool bSet)
{
	if (OverlayedCharacter)
		OverlayedCharacter->SetOverlayForInstigator(bSet, OverlayedCharacter->OverlayMaterialForShield);
}

// Called when the game starts
void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ADDCharacter *AddCharacter = Cast<ADDCharacter>(GetOwner()))
	{
		ProjectileShooterComp = AddCharacter->GetProjectileShooterComponent();
		StatComponent = AddCharacter->GetStatComponent();
		
		if (!StatComponent->OnDamagedDelegate.IsAlreadyBound(this, &UCombatComponent::OnDamaged))
			StatComponent->OnDamagedDelegate.AddDynamic(this, &UCombatComponent::OnDamaged);
		MY_LOG(LogTemp, Log, TEXT("On Damaged Dynamic binded"));
	}

	SetComponentTickEnabled(false);
	
}


void UCombatComponent::ResetAttackCombo()
{
	StopMontage(0.2f, AttackAnimMontage[0]);
	ComboCount = 0;
}

void UCombatComponent::ResetSkillCombo()
{
	SkillComboCount = 0;
}

void UCombatComponent::ResetBeforeAttack()
{
	//공격 시 은신 풀림
	if (bStealthed) EndStealth();
}

void UCombatComponent::StartQCoolTime()
{
	Client_StartQCoolTime();
	GetWorld()->GetTimerManager().SetTimer(QCoolTimeHandle, this, &UCombatComponent::QReady, QCoolTime, false);
	//MY_LOG(LogTemp, Warning, TEXT("QCoolTimeStart : %f"), QCoolTime);
}

void UCombatComponent::StartECoolTime()
{
	Client_StartECoolTime();

	float NewECoolTime = ECoolTime;

	// 쌍검일 때 은신 대기시간이 줄어든다
	if(DDCharacter->WeaponMode == EWeaponMode::DoubleSword)
	{
		ECoolTime /= 3;
	}
	
	GetWorld()->GetTimerManager().SetTimer(ECoolTimeHandle, this, &UCombatComponent::EReady, NewECoolTime, false);
	//MY_LOG(LogTemp, Warning, TEXT("ECoolTimeStart : %f"), ECoolTime);
}

void UCombatComponent::StartRCoolTime()
{
	Client_StartRCoolTime();
	GetWorld()->GetTimerManager().SetTimer(RCoolTimeHandle, this, &UCombatComponent::RReady, RCoolTime, false);
	//MY_LOG(LogTemp, Warning, TEXT("RCoolTimeStart : %f"), RCoolTime);
}

void UCombatComponent::StartDashCoolTime()
{
	Client_StartDashCoolTime();
	GetWorld()->GetTimerManager().SetTimer(DashCoolTimeHandle, this, &UCombatComponent::DashReady, DashCoolTime, false);
}

void UCombatComponent::StartBlockCoolTime()
{
	Client_StartBlockCoolTime();
	GetWorld()->GetTimerManager().SetTimer(BlockCoolTimeHandle, this, &UCombatComponent::BlockReady, BlockCoolTime, false);
	//MY_LOG(LogTemp, Warning, TEXT("BlockCoolTimeStart : %f"), BlockCoolTime);
}

void UCombatComponent::Client_StartQCoolTime_Implementation()
{
	
	if(AIngamePlayerController* MyPC = Cast<AIngamePlayerController>(DDCharacter->GetController()))
	{
		if(MyPC->IngameHUD && MyPC->IngameHUD->WBP_Skill_Q)
		{
			MyPC->IngameHUD->WBP_Skill_Q->StartCooldown(QCoolTime);
		}
	}
}

void UCombatComponent::Client_StartECoolTime_Implementation()
{
	if(AIngamePlayerController* MyPC = Cast<AIngamePlayerController>(Cast<APawn>(GetOwner())->GetController()))
	{
		if(MyPC->IngameHUD && MyPC->IngameHUD->WBP_Skill_E)
		{
			MyPC->IngameHUD->WBP_Skill_E->StartCooldown(ECoolTime);
			//MY_LOG(LogTemp, Log, TEXT("ECoolTime %f"), ECoolTime);
		}
	}
}

void UCombatComponent::Client_StartRCoolTime_Implementation()
{
	if(AIngamePlayerController* MyPC = Cast<AIngamePlayerController>(Cast<APawn>(GetOwner())->GetController()))
	{
		if(MyPC->IngameHUD && MyPC->IngameHUD->WBP_Skill_R)
		{
			MyPC->IngameHUD->WBP_Skill_R->StartCooldown(RCoolTime);
		}
	}
}

void UCombatComponent::Client_StartBlockCoolTime_Implementation()
{
	//Block CoolTime Widget 설정 추가
	if(AIngamePlayerController* MyPC = Cast<AIngamePlayerController>(Cast<APawn>(GetOwner())->GetController()))
	{
		if(MyPC->IngameHUD && MyPC->IngameHUD->WBP_Skill_Block)
		{
			MyPC->IngameHUD->WBP_Skill_Block->StartCooldown(BlockCoolTime);
		}
	}
}


void UCombatComponent::Client_StartDashCoolTime_Implementation()
{
	//Dash CoolTime Widget 설정 추가
	if(AIngamePlayerController* MyPC = Cast<AIngamePlayerController>(Cast<APawn>(GetOwner())->GetController()))
	{
		if(MyPC->IngameHUD && MyPC->IngameHUD->WBP_Skill_Dash)
		{
			MyPC->IngameHUD->WBP_Skill_Dash->StartCooldown(DashCoolTime);
		}
	}
}


void UCombatComponent::QReady()
{
	bQReady = true;
	//MY_LOG(LogTemp, Error, TEXT("QReady"));
}

void UCombatComponent::EReady()
{
	bEReady = true;
	//MY_LOG(LogTemp, Error, TEXT("EReady"));
}

void UCombatComponent::RReady()
{
	bRReady = true;
	//MY_LOG(LogTemp, Error, TEXT("RReady"));
}

void UCombatComponent::BlockReady()
{
	bBlockReady = true;
	//MY_LOG(LogTemp, Error, TEXT("Block Ready"));
}

void UCombatComponent::DashReady()
{
	bDashReady = true;
}

void UCombatComponent::Block()
{
	FAction BlockAction(this, Attacking, FName("Block_Action"), FName("Block_Cancel"), FName("BlockEnd"), 3, 5, FName("Block"));
	Server_TryPlayAction(BlockAction);
}

void UCombatComponent::MC_Block_Implementation()
{
	UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		int32 index = static_cast<int32>(DDCharacter->WeaponMode);
		// 블렌드 설정 (블렌드 인, 블렌드 아웃 모두 0으로 설정)d 
		FAlphaBlendArgs BlendSettings;
		BlendSettings.BlendTime = 0.0f; // 블렌드 인/아웃 시간 0초
		
		AnimInstance->Montage_PlayWithBlendSettings(BlockMontage, BlendSettings);
	}
}

void UCombatComponent::BlockEnd()
{
	if(StatComponent)
	{
		UBaseEffect* PlayerGuardEffect = StatComponent->FindEffectByName("PlayerGuard");
		if(PlayerGuardEffect)
		{
			StatComponent->RemoveEffect(PlayerGuardEffect);
		}
	}
	bIsBlocking = false;
	StartBlockCoolTime();
	StopMontage(0.f, BlockMontage);
	SetDefaultAction();
}

//넉백은 모든 몽타주를 중지시키고 캐릭터를 넘어트린다. 넉백 애니메이션 재생하는 동안 아무 행동도 할 수 없다.
void UCombatComponent::KnockBackCharacter()
{
	if (!CanPlayAction(1)) return;
	//모든 몽타주 중지
	StopAllMontages();
	//왜인지 모르게 방해가 브로드캐스트 안됨, 명시적 호출
	ResetBoolValByKnockbacked();
	
	bKnockbacked = true;
	KnockBackRandIndex = FMath::RandRange(0, 1);

	MC_KnockBackMontage(KnockBackRandIndex);

	FTimerDelegate TimerDelegate = FTimerDelegate::CreateWeakLambda(this, [this]
	{
		CL_SetbCanLook(true);
		bKnockbacked = false;
	});

	//혹시모를 초기화 코드
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, 0.5f, false);
}

void UCombatComponent::Attack_Action()
{
	ResetBeforeAttack();
	bIsAttacking = true;
	MC_PlayAttackMontage(ComboCount);
}

void UCombatComponent::SkillQ_Action()
{
	bQReady = false;
	bSkillQ = true;
	bIsAttacking = false;
		
	MC_PlayQMontage();
}

void UCombatComponent::SkillE_Action()
{
	bEReady = false;
	bSkillE = true;
	bIsAttacking = false;
		
	MC_PlayEMontage();
}

void UCombatComponent::SkillR_Action()
{
	bRReady = false;
	bSkillR = true;
	bIsAttacking = false;
		
	MC_PlayRMontage();
}

void UCombatComponent::Dash_Action()
{
	bIsDashing = true;
	bIsAttacking = false;
	bDashReady = false;

	//Dash 중엔 방해를 받지 않는다.
	MC_Dash(DashSide);
}

void UCombatComponent::Block_Action()
{
	if (bBlockReady)
	{
		bIsAttacking = false;
		bIsBlocking = true;
		bBlockReady = false;
		
		//0.3초간 지속되는 Guard Effect 생성
		UGuardEffect* GuardEffect = NewObject<UGuardEffect>();
		GuardEffect->Initialize(DDCharacter, 0.f, 0.3f);
		StatComponent->ApplyEffect(GuardEffect);

		//막기 애니메이션 지속 시간은 1초
		FTimerHandle BlockDurationHandle;
		GetWorld()->GetTimerManager().SetTimer(BlockDurationHandle, this, &UCombatComponent::BlockEnd, 1.f, false);
		
		MC_Block();
	}
}

void UCombatComponent::MC_PlayQMontage_Implementation()
{
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)           
	{
		AnimInstance->Montage_Play(SkillAnimMontage[Skill_Q]);
	}
}

void UCombatComponent::MC_PlayEMontage_Implementation()
{
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)           
	{
		AnimInstance->Montage_Play(SkillAnimMontage[Skill_E]);
	}
}

void UCombatComponent::MC_PlayRMontage_Implementation()
{
	USkeletalMeshComponent* SkeletalMesh = DDCharacter->GetMesh();
	UAnimInstance* AnimInstance = SkeletalMesh->GetAnimInstance();
	if (AnimInstance)           
	{
		AnimInstance->Montage_Play(SkillAnimMontage[Skill_R]);
	}
}

void UCombatComponent::MC_KnockBackMontage_Implementation(int RandNumber)
{
	UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance();
	KnockBackRandIndex = RandNumber;
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(KnockBackMontage[RandNumber]);
	}
}

void UCombatComponent::BigKnockBackCharacter()
{
	if (!CanPlayAction(0)) return;
	
	StopAllMontages();
	CL_SetbCanLook(false);
	bBigKnockbacked = true;

	ResetBoolValByKnockbacked();

	MC_BigKnockBackMontage();

	FTimerDelegate TimerDelegate = FTimerDelegate::CreateWeakLambda(this, [this]
	{
		CL_SetbCanLook(true);
		ResetBoolValByKnockbacked();
		
		bBigKnockbacked = false;
	});

	//혹시모를 초기화 코드
	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, 2.2f, false);
}

void UCombatComponent::MC_BigKnockBackMontage_Implementation()
{
	UAnimInstance *AnimInstance = DDCharacter->GetMesh()->GetAnimInstance();
	if (AnimInstance)
	{
		AnimInstance->Montage_Play(BigKnockBackMontage);
	}
}

void UCombatComponent::ShockCharacter(float Duration)
{
	bShocked = true;
	MC_ShockParticle(true);
	if (IngamePlayerController)
	{
		IngamePlayerController->Client_BanSkillImage(true, true, true, true);
	}

	FTimerDelegate TimerDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		bShocked = false;
		MC_ShockParticle(false);
		if (IngamePlayerController)
		{
			IngamePlayerController->Client_BanSkillImage(false, true, true, true);
		}
	});
	
	GetWorld()->GetTimerManager().SetTimer(ShockTimerHandle, TimerDelegate, Duration, false);
}

void UCombatComponent::MC_ShockParticle_Implementation(bool bInShocked)
{
	if (bInShocked)
	{
		if (!ShockComp)
			ShockComp = UGameplayStatics::SpawnEmitterAttached(ShockParticle, DDCharacter->GetMesh(), "root");
	}
	else
	{
		if (ShockComp)
		{
			ShockComp->DestroyComponent();
			ShockComp = nullptr;
		}
	}
}

void UCombatComponent::MC_BlockSuccess_Implementation()
{
	FVector SpawnLocation = DDCharacter->GetMesh()->GetSocketLocation("SkillActorSpawn");
	if (BlockSuccessEffect2)
		UParticleSystemComponent *ParticleSystemComponent = UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), BlockSuccessEffect2, SpawnLocation);
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombatComponent, QCoolTime);
	DOREPLIFETIME(UCombatComponent, ECoolTime);
	DOREPLIFETIME(UCombatComponent, RCoolTime);
	DOREPLIFETIME(UCombatComponent, BlockCoolTime);
	DOREPLIFETIME(UCombatComponent, DashCoolTime);
	DOREPLIFETIME(UCombatComponent, AttackSpeed);
	DOREPLIFETIME(UCombatComponent, bAimingToGiveShield);
	DOREPLIFETIME(UCombatComponent, bSkillE);
	DOREPLIFETIME(UCombatComponent, bIsAttacking);
	DOREPLIFETIME(UCombatComponent, bEReady);
	DOREPLIFETIME(UCombatComponent, bQReady);
	DOREPLIFETIME(UCombatComponent, bRReady);
	DOREPLIFETIME(UCombatComponent, bBlockReady);
	DOREPLIFETIME(UCombatComponent, bDashReady);
	DOREPLIFETIME(UCombatComponent, SkillComboCount);
	DOREPLIFETIME(UCombatComponent, CurAction);
}


void UCombatComponent::MC_SetWalkSpeed_Implementation(float walkspeed)
{
	DDCharacter->GetCharacterMovement()->MaxWalkSpeed = walkspeed;
}

//서버 호출 해야 한다.
bool UCombatComponent::SphereTrace(FVector StartLocation, FVector EndLocation, float SphereRadius,
	TArray<FHitResult> &HitResults, bool bTraceCharacter)
{
	TArray<FHitResult> TempHitResults;

	TArray<TEnumAsByte<EObjectTypeQuery>> types;
	types.Add(UEngineTypes::ConvertToObjectType(ECC_GameTraceChannel1));
	types.Add(UEngineTypes::ConvertToObjectType(ECC_GameTraceChannel9));
	types.Add(UEngineTypes::ConvertToObjectType(ECC_WorldStatic));
	types.Add(UEngineTypes::ConvertToObjectType(ECC_WorldDynamic));
	if(bTraceCharacter)
	{
		types.Add(UEngineTypes::ConvertToObjectType(ECC_GameTraceChannel2));
	}
	
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(DDCharacter);
	// Sphere Trace 실행
	bool bHit = UKismetSystemLibrary::SphereTraceMultiForObjects(
		this,
		StartLocation,
		EndLocation,
		SphereRadius,
		types, // 충돌 채널
		false,                                           // Trace 복잡성
		IgnoreActors,                               // 무시할 액터들
		EDrawDebugTrace::ForDuration,                   // 디버그 타입
		TempHitResults,                                       // 결과 저장
		true,                                            // 본인 제외
		FLinearColor::Red,                               // 디버그 선 색상
		FLinearColor::Green,                             // 디버그 구 색상
		2.0f                                             // 디버그 지속 시간
	);

	if (bHit)
	{
		//중복 방징용
		TSet<AActor*> AddedActors;
		
		for(FHitResult &HitResult : TempHitResults)
		{
			AActor* const HitActor = HitResult.GetActor();
			if (!HitActor) continue;

			//중복 액터는 무시
			if (AddedActors.Contains(HitActor)) continue;
			
			if ((bTraceCharacter && Cast<ADDCharacter>(HitActor)) ||
				Cast<AMonsterBase>(HitActor) ||
				Cast<ADamageableActor>(HitActor) ||
				Cast<ASkillActorHaveStatComp>(HitActor) ||
				Cast<ABuildingBase>(HitActor))
			{
				AddedActors.Add(HitActor);
				HitResults.Add(HitResult);
			}
		}

		if (HitResults.Num() > 0)
		{
			CrosshairHitReaction();
			return true;
		}
	}
	return false;
}

void UCombatComponent::CrosshairHitReaction()
{
	// Crosshair Reaction
	if(!GetOwner()) return;
	AController* Controller = Cast<ACharacter>(GetOwner())->Controller;
	AIngamePlayerController* InGamePlayerController = Cast<AIngamePlayerController>(Controller);
	if(InGamePlayerController)
	{
		InGamePlayerController->Client_CrossHairHitReact();
	}
}

void UCombatComponent::ApplyCombatDamage(AActor* TargetActor, float AdScale, float ApScale, bool bHasKnockback, EDamageType DamageType, EAttackType AttackType, FName SkillName, EHitEffectState HitEffectState, FVector HitLocation)
{
	UCharacterStatComponent* CharacterStatComponent = Cast<ADDCharacter>(GetOwner())->GetStatComponent();
	if(!CharacterStatComponent) return;

	if(AttackType == EAttackType::NormalAttack)
	{
		DDCharacter->OnNormalAttackHit.Broadcast(TargetActor);
	}
	
	UMonsterStatComponent* MonsterStatComponent = nullptr;
	if (AMonsterBase *MonsterBase = Cast<AMonsterBase>(TargetActor))
	{
		MonsterStatComponent = MonsterBase->GetStatComponent();

		//HitEffect 호출
		if (HitLocation != FVector::ZeroVector)
		{
			FVector RelativeLocation = HitLocation - TargetActor->GetActorLocation();
			//MY_LOG(LogTemp, Log, TEXT("RelativeLocation = %f, %f, %f"), RelativeLocation.X, RelativeLocation.Y, RelativeLocation.Z);
			MonsterBase->GetHitEffectComponent()->Multicast_SpawnEffect(static_cast<int>(HitEffectState), nullptr, RelativeLocation);
		}
		else
			MonsterBase->GetHitEffectComponent()->Multicast_SpawnEffect(static_cast<int>(HitEffectState), nullptr, TargetActor->GetActorLocation());
	}
	else if(ASkillActorHaveStatComp *SkillActorHaveStatComp = Cast<ASkillActorHaveStatComp>(TargetActor))
	{
		MonsterStatComponent = SkillActorHaveStatComp->GetMonsterStatComponent();
	}
	else if (ADamageableActor *DamageableActor = Cast<ADamageableActor>(TargetActor))
	{
		DamageableActor->Damaged(DDCharacter);
		return;
	}
	else
	{
		return;
	}
	
	float CharacterAD = CharacterStatComponent->GetFinalDamage(EDamageType::AdDamage);
	float CharacterAP = CharacterStatComponent->GetFinalDamage(EDamageType::ApDamage);

	if(DamageType == EDamageType::AdDamage)
	{
		MonsterStatComponent->ApplyDamage(GetOwner(), DamageType, CharacterAD * AdScale, bHasKnockback, AttackType, SkillName);
		return;
	}
	if(DamageType == EDamageType::ApDamage)
	{
		MonsterStatComponent->ApplyDamage(GetOwner(), DamageType, CharacterAP * ApScale, bHasKnockback, AttackType, SkillName);
	}
}


//화면 크로스헤어에 맞는 projectile의 발사 Rotation과 Location 값을 넣는다. 없을 시 멀리 있는 적을 맞추는 느낌으로 조정한다.
bool UCombatComponent::FindTransformToShootProjectile(FVector& Location, FRotator& Rotation, const bool bHaveGravity) const
{
	Location = DDCharacter->GetMesh()->GetSocketLocation("SkillActorSpawn");
	
	FVector StartPos = DDCharacter->CameraComp->GetComponentLocation();
	FVector EndPos = StartPos + DDCharacter->CameraComp->GetForwardVector() * 5000.f;
	
	Rotation = (EndPos - Location).Rotation();

	if (bHaveGravity)
	{
		Rotation.Pitch += 3.f;
		return true;
	}
	
	FHitResult HitResult;
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartPos, EndPos, ECC_Visibility);
	if (bHit)
	{
		const float DistFromLocation = FVector::Dist(Location, HitResult.ImpactPoint);

		//MY_LOG(LogTemp, Log, TEXT("Dist From Hit Location  %f, Max : %f"), DistFromLocation, 1200.f);
		if (DistFromLocation > 1200.f)
		{
			EndPos = HitResult.ImpactPoint;
			Rotation = (EndPos - Location).Rotation();
		}

		// 너무 가까운 경우엔 Rotation을 적절하게 설정해준다.
		else
		{
			EndPos = StartPos + DDCharacter->CameraComp->GetForwardVector() * 2000.f;
			Rotation = (EndPos - Location).Rotation();
		}
		
		 DrawDebugSphere(GetWorld(), EndPos, 25.f, 12, FColor::Green, false, 1.0f, 0, 2.0f);
		 DrawDebugDirectionalArrow(GetWorld(), Location, EndPos, 50.f, FColor::Green, false, 1.0f, 0, 3.f);
	}
	//else MY_LOG(LogTemp, Log, TEXT("No Hit"));

	return bHit;
}


// Called every frame
void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bAimingToGiveShield)
	{
		//Trace를 발사해서 맞는 DDCharacter를 Overlay로 빛나게 한다.
		FVector StartLocation = DDCharacter->CameraComp->GetComponentLocation();
		FVector ForwardVector = DDCharacter->CameraComp->GetForwardVector();
		FVector EndLocation = StartLocation +  ForwardVector * 4000;

		TArray<AActor*> ActorsToIgnore;
		ActorsToIgnore.Add(DDCharacter);

		ETraceTypeQuery TraceType = UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_GameTraceChannel8);

		FHitResult HitResult;
		bool bHit = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), StartLocation, EndLocation, 50.f , TraceType, false,  ActorsToIgnore, EDrawDebugTrace::ForOneFrame, HitResult, true);
		if (bHit)
		{
			// Line Trace가 히트한 경우
			FVector HitLocation = HitResult.ImpactPoint; // 충돌 위치

			// 디버그용 라인 표시
			if (ADDCharacter *ddCharacter = Cast<ADDCharacter>(HitResult.GetActor()))
			{
				//자기 자신은 무시
				if (ddCharacter == DDCharacter) return;
				
				if (OverlayedCharacter)
				{
					if (OverlayedCharacter != ddCharacter)
					{
						if (DDCharacter->IsLocallyControlled() && !bChangingOverlay)
						{
							bChangingOverlay = true;
							MY_LOG(LogTemp, Log, TEXT("Other OvelayCharacter = %s"), *GetNameSafe(ddCharacter));
							OverlayedCharacter->SetOverlayForInstigator(false, OverlayedCharacter->OverlayMaterialForShield);
							ddCharacter->SetOverlayForInstigator(true, OverlayedCharacter->OverlayMaterialForShield);
							bChangingOverlay = false;
						}
						
						OverlayedCharacter = ddCharacter;
					}
					//else MY_LOG(LogTemp, Log, TEXT("Overlayed Character = %s"), *GetNameSafe(OverlayedCharacter));
				}
				else
				{
					OverlayedCharacter = ddCharacter;
					if (DDCharacter->IsLocallyControlled() && !bChangingOverlay)
					{
						bChangingOverlay = true;
						MY_LOG(LogTemp, Log, TEXT("new OvelayCharacter = %s"), *GetNameSafe(ddCharacter));
						ddCharacter->SetOverlayForInstigator(true, OverlayedCharacter->OverlayMaterialForShield);
						bChangingOverlay = false;
					}
				}
			}
		}
		else if (OverlayedCharacter)
		{
			if (DDCharacter->IsLocallyControlled() && !bChangingOverlay)
			{
				bChangingOverlay = true;
				MY_LOG(LogTemp, Log, TEXT("overlay off = %s"), *GetNameSafe(OverlayedCharacter));
				OverlayedCharacter->SetOverlayForInstigator(false, OverlayedCharacter->OverlayMaterialForShield);
				bChangingOverlay = false;
			}
			OverlayedCharacter = nullptr;
		}
	}
	else if (bDarkMagicOrbSkill)
	{
		if (DDCharacter && DDCharacter->CameraComp)
		{
			FVector StartLocation = DDCharacter->CameraComp->GetComponentLocation();
			FVector ForwardVector = DDCharacter->CameraComp->GetForwardVector();
			FVector EndLocation = StartLocation +  ForwardVector * 4000;

			FCollisionQueryParams Params;
			Params.bTraceComplex = true;
			Params.AddIgnoredActor(DDCharacter);

			FHitResult HitResult;
			bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECC_Visibility, Params);
			if (bHit)
			{
				// Line Trace가 히트한 경우
				FVector HitLocation = HitResult.ImpactPoint; // 충돌 위치

				// 디버그용 라인 표시
				// DrawDebugLine(GetWorld(), StartLocation, HitLocation, FColor::Green, false, 1.0f, 0, 1.0f);
				// DrawDebugPoint(GetWorld(), HitLocation, 10.0f, FColor::Red, false, 1.0f);

				// DecalActor 위치 업데이트
				DecalLocation = HitLocation;
				
				 DecalMagicOrbSkill->SetActorLocation(HitLocation);
				 DecalMagicOrbSkill->SetActorRotation(FRotator(90.0f, 0.0f, 0.0f)); // 표면에 맞게 회전
			}
		
		}
	}
}

